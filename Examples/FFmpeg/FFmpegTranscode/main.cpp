#include <FFMpeg/FFMpegCommon.h>
#include <WindowManagement/GLFWWindow.h>
#include <GeoPrimitives/Quad.h>
#include <GLBase.h>
#include <StopWatch.h>

#include "Asset/AssetImageBase.h"
#include "FFMpeg/FFMpegPlayer.h"

using namespace std;
using namespace ara;
using namespace ara::av;

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;
typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;

    AVPacket *enc_pkt;
    AVFrame *filtered_frame;
} FilteringContext;
static FilteringContext *filter_ctx;

typedef struct StreamContext {
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;

    AVFrame *dec_frame;
} StreamContext;
static StreamContext *stream_ctx;

GLBase              glbase;
StopWatch           fpsWatch;
StopWatch           decodeWatch;
GLFWwindow*		    window = nullptr;
ShaderCollector     shCol;
unique_ptr<Quad>    quad;
FFMpegPlayer        fpl;
bool                glInited = false;

static void openInputFile(const char *filename) {
    int ret;

    ifmt_ctx = nullptr;
    if ((ret = avformat_open_input(&ifmt_ctx, filename, nullptr, nullptr)) < 0) {
        throw runtime_error("Cannot open input file");
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, nullptr)) < 0) {
        throw runtime_error("Cannot find stream information");
    }

    stream_ctx = static_cast<StreamContext *>(av_calloc(ifmt_ctx->nb_streams, sizeof(*stream_ctx)));
    if (!stream_ctx) {
        throw runtime_error(std::to_string( AVERROR(ENOMEM) ) );
    }

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        const auto stream = ifmt_ctx->streams[i];
        const auto dec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!dec) {
            throw runtime_error("Failed to find decoder for stream #"+std::to_string(i));
        }
        const auto codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx) {
            throw runtime_error("Failed to allocate the decoder context for stream  "+std::to_string(i));
        }

        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        if (ret < 0) {
            throw runtime_error("Failed to copy decoder parameters to input decoder context for stream "+std::to_string(i));
        }

        // Inform the decoder about the timebase for the packet timestamps. This is highly recommended, but not mandatory.
        codec_ctx->pkt_timebase = stream->time_base;

        // Reencode video & audio and remux subtitles etc.
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                codec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, nullptr);
            }
            // Open decoder
            ret = avcodec_open2(codec_ctx, dec, nullptr);
            if (ret < 0) {
                throw runtime_error("Failed to open decoder for stream "+std::to_string(i));
            }
        }
        stream_ctx[i].dec_ctx = codec_ctx;
        stream_ctx[i].dec_frame = av_frame_alloc();
        if (!stream_ctx[i].dec_frame) {
            throw runtime_error(std::to_string( AVERROR(ENOMEM) ) );
        }
    }

    av_dump_format(ifmt_ctx, 0, filename, 0);
}

static void openOutputFile(const char *filename) {
    int ret;

    ofmt_ctx = nullptr;
    avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename);
    if (!ofmt_ctx) {
        throw runtime_error("Could not create output context\n");
    }


    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            throw runtime_error("Failed allocating output stream");
        }

        const auto inStream = ifmt_ctx->streams[i];
        const auto decCtx = stream_ctx[i].dec_ctx;

        if (decCtx->codec_type == AVMEDIA_TYPE_VIDEO || decCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
            // in this example, we choose transcoding to same codec
            const AVCodec *encoder = avcodec_find_encoder(decCtx->codec_id);
            if (!encoder) {
                throw runtime_error("Necessary encoder not found");
            }

            const auto enc_ctx = avcodec_alloc_context3(encoder);
            if (!enc_ctx) {
                throw runtime_error("Failed to allocate the encoder context");
            }

            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (decCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->height = decCtx->height;
                enc_ctx->width = decCtx->width;
                enc_ctx->sample_aspect_ratio = decCtx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = decCtx->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                enc_ctx->time_base = av_inv_q(decCtx->framerate);
            } else {
                enc_ctx->sample_rate = decCtx->sample_rate;
                ret = av_channel_layout_copy(&enc_ctx->ch_layout, &decCtx->ch_layout);
                if (ret < 0) {
                    throw runtime_error("av_channel_layout_copy failed");
                }
                // take first format from list of supported formats
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            }

            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            // Third parameter can be used to pass settings to encoder
            ret = avcodec_open2(enc_ctx, encoder, nullptr);
            if (ret < 0) {
                throw runtime_error("Cannot open video encoder for stream #"+std::to_string(i));
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                throw runtime_error("Failed to copy encoder parameters to output stream #"+std::to_string(i));
            }

            out_stream->time_base = enc_ctx->time_base;
            stream_ctx[i].enc_ctx = enc_ctx;
        } else if (decCtx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            throw runtime_error("Elementary stream #"+std::to_string(i)+" is of unknown type, cannot proceed");
        } else {
            // if this stream must be remuxed
            ret = avcodec_parameters_copy(out_stream->codecpar, inStream->codecpar);
            if (ret < 0) {
                throw runtime_error("Copying parameters for stream #"+std::to_string(i)+" failed");
            }
            out_stream->time_base = inStream->time_base;
        }

    }

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            throw runtime_error("Could not open output file "+std::string(filename));
        }
    }

    // init muxer, write output file header
    ret = avformat_write_header(ofmt_ctx, nullptr);
    if (ret < 0) {
        throw runtime_error("Error occurred when opening output file");
    }

    LOG << "\n------------------------\n";

    av_dump_format(ofmt_ctx, 0, filename, 1);
}

static int initFilter(FilteringContext* fctx, AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, const char *filter_spec) {
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = nullptr;
    const AVFilter *buffersink = nullptr;
    AVFilterContext *buffersrc_ctx = nullptr;
    AVFilterContext *buffersink_ctx = nullptr;
    auto outputs = avfilter_inout_alloc();
    auto inputs  = avfilter_inout_alloc();
    auto filter_graph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(nullptr, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                dec_ctx->pkt_timebase.num, dec_ctx->pkt_timebase.den,
                dec_ctx->sample_aspect_ratio.num,
                dec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, nullptr, filter_graph);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", nullptr, nullptr, filter_graph);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                reinterpret_cast<uint8_t *>(&enc_ctx->pix_fmt), sizeof(enc_ctx->pix_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        char buf[64];
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(nullptr, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (dec_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&dec_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);
        av_channel_layout_describe(&dec_ctx->ch_layout, buf, sizeof(buf));
        snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                dec_ctx->pkt_timebase.num, dec_ctx->pkt_timebase.den, dec_ctx->sample_rate,
                av_get_sample_fmt_name(dec_ctx->sample_fmt),
                buf);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, nullptr, filter_graph);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                nullptr, nullptr, filter_graph);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                reinterpret_cast<uint8_t *>(&enc_ctx->sample_fmt), sizeof(enc_ctx->sample_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        av_channel_layout_describe(&enc_ctx->ch_layout, buf, sizeof(buf));
        ret = av_opt_set(buffersink_ctx, "ch_layouts",
                         buf, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                reinterpret_cast<uint8_t *>(&enc_ctx->sample_rate), sizeof(enc_ctx->sample_rate),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    // Endpoints for the filter graph.
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, nullptr)) < 0) {
        goto end;
    }

    if ((ret = avfilter_graph_config(filter_graph, nullptr)) < 0) {
        goto end;
    }

    // Fill FilteringContext
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int initFilters() {
    const char *filter_spec;
    filter_ctx = static_cast<FilteringContext*>(av_malloc_array(ifmt_ctx->nb_streams, sizeof(*filter_ctx)));
    if (!filter_ctx) {
        return AVERROR(ENOMEM);
    }

    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        filter_ctx[i].buffersrc_ctx  = nullptr;
        filter_ctx[i].buffersink_ctx = nullptr;
        filter_ctx[i].filter_graph   = nullptr;
        if (!(ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
              || ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)) {
            continue;
        }


        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            filter_spec = "null"; // passthrough (dummy) filter for video
        } else {
            filter_spec = "anull"; // passthrough (dummy) filter for audio
        }

        if (int ret = initFilter(&filter_ctx[i], stream_ctx[i].dec_ctx, stream_ctx[i].enc_ctx, filter_spec)) {
            return ret;
        }

        filter_ctx[i].enc_pkt = av_packet_alloc();
        if (!filter_ctx[i].enc_pkt) {
            return AVERROR(ENOMEM);
        }

        filter_ctx[i].filtered_frame = av_frame_alloc();
        if (!filter_ctx[i].filtered_frame) {
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static int encodeWriteFrame(unsigned int stream_index, int flush) {
    const auto stream = &stream_ctx[stream_index];
    const auto filter = &filter_ctx[stream_index];
    auto filt_frame = flush ? nullptr : filter->filtered_frame;
    const auto enc_pkt = filter->enc_pkt;

    // encode filtered frame
    av_packet_unref(enc_pkt);

    if (filt_frame && filt_frame->pts != AV_NOPTS_VALUE) {
        filt_frame->pts = av_rescale_q(filt_frame->pts, filt_frame->time_base, stream->enc_ctx->time_base);
    }

    int ret = avcodec_send_frame(stream->enc_ctx, filt_frame);
    if (ret < 0) {
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(stream->enc_ctx, enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }

        // prepare packet for muxing
        enc_pkt->stream_index = stream_index;
        av_packet_rescale_ts(enc_pkt,
                             stream->enc_ctx->time_base,
                             ofmt_ctx->streams[stream_index]->time_base);

        // mux encoded frame
        ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt);
    }

    return ret;
}

static int filterEncodeWriteFrame(AVFrame *frame, unsigned int stream_index) {
    auto filter = &filter_ctx[stream_index];

    // push the decoded frame into the filtergraph
    int ret = av_buffersrc_add_frame_flags(filter->buffersrc_ctx, frame, 0);
    if (ret < 0) {
        throw runtime_error("Error while feeding the filtergraph");
    }

    // pull filtered frames from the filtergraph
    while (true) {
        ret = av_buffersink_get_frame(filter->buffersink_ctx, filter->filtered_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                ret = 0;
            }
            break;
        }

        filter->filtered_frame->time_base = av_buffersink_get_time_base(filter->buffersink_ctx);;
        filter->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encodeWriteFrame(stream_index, 0);
        av_frame_unref(filter->filtered_frame);
        if (ret < 0) {
            break;
        }
    }

    return ret;
}

static int flushEncoder(unsigned int stream_index) {
    if (!(stream_ctx[stream_index].enc_ctx->codec->capabilities & AV_CODEC_CAP_DELAY)) {
        return 0;
    }
    return encodeWriteFrame(stream_index, 1);
}

static int cleanUp(AVPacket* packet, int ret) {
    av_packet_free(&packet);
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        avcodec_free_context(&stream_ctx[i].dec_ctx);

        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && stream_ctx[i].enc_ctx) {
            avcodec_free_context(&stream_ctx[i].enc_ctx);
        }

        if (filter_ctx && filter_ctx[i].filter_graph) {
            avfilter_graph_free(&filter_ctx[i].filter_graph);
            av_packet_free(&filter_ctx[i].enc_pkt);
            av_frame_free(&filter_ctx[i].filtered_frame);
        }

        av_frame_free(&stream_ctx[i].dec_frame);
    }
    av_free(filter_ctx);
    av_free(stream_ctx);
    avformat_close_input(&ifmt_ctx);

    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmt_ctx->pb);
    }

    avformat_free_context(ofmt_ctx);

    if (ret < 0) {
        LOGE << "Error occurred: " << av_err2str(ret);
    }
    return ret ? 1 : 0;
}

static void output_error(int error, const char* msg) {
    LOGE << "Error:" << std::string(msg);
}

static void initGlfw(AVFrame* frame) {
    glfwSetErrorCallback(output_error);
    if (!glfwInit()) {
        LOGE << "Failed to initialize GLFW";
    }
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    glfwWindowHint(GLFW_DECORATED, GL_TRUE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GL_TRUE); // if GL_FALSE pixel sizing is 1:1 if GL_TRUE the required size will be different from the resulting window size
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

    window = glfwCreateWindow(frame->width, frame->height, "FFMpeg Re-encode Test", nullptr, nullptr);
    if (!window) {
        LOGE << "Failed to create GLFW window";
        glfwTerminate();
    }

    glfwWindowHint(GLFW_SAMPLES, 2);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_TRUE);
    glfwSetWindowPos(window, 0, 0);
    glfwShowWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // run as fast as possible

    LOG << "Vendor:   " << glGetString(GL_VENDOR);
    LOG << "Renderer: " << glGetString(GL_RENDERER);
    LOG << "Version:  " << glGetString(GL_VERSION);
    LOG << "GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION);
}

static void glResInit(AVFrame* frame, AVCodecContext* ctx) {
    initGlfw(frame);
    initGLEW();
    glbase.init(false);
    glfwMakeContextCurrent(window);
    glViewport(0, 0, frame->width, frame->height);

    auto& decPar = fpl.getPar();
    decPar.destWidth = frame->width;
    decPar.destHeight = frame->height;
    decPar.decodeYuv420OnGpu = true;
    decPar.glbase = &glbase;

    fpl.setShaderCollector(&glbase.shaderCollector());
    fpl.setVideoContext(ctx);
    fpl.allocateResources(decPar);
    fpl.getSrcWidth() = frame->width;
    fpl.getSrcHeight() = frame->height;
    fpl.setSrcPixFmt(ctx->pix_fmt);
    fpl.setRunning(true);
    fpl.getFramesCycleBuf().feedCountUp();

    quad = make_unique<Quad>(QuadInitParams{ .color = glm::vec4{0.f, 0.f, 0.f, 1.f}, .flipHori = true });
}

static void drawOnTop(AVFrame* frame, AVCodecContext* ctx) {
    if (!glInited) {
        glResInit(frame, ctx);
        glInited = true;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto pts = frame->pts;

    auto& cb = fpl.getFramesCycleBuf();
    cb.feedCountUp();
    cb.getReadBuff().frame = frame;
    fpl.loadFrameToTexture(0.0, true);

    fpl.shaderBegin(); // draw with conversion yuv -> rgb on gpu
    quad->draw();

    frame->pts = pts;

    /*
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glReadPixels(0, 0, frame->width, frame->height, GL_BGRA, GL_UNSIGNED_BYTE, frame->data);  // synchronous, blo
*/
    glfwSwapBuffers(window);
}

int main(int argc, char **argv) {
    decodeWatch.setStart();

    int ret = 0;

    if (argc != 3) {
        av_log(nullptr, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }

    AVPacket *packet = nullptr;
    try {
        unsigned int stream_index{};
        openInputFile(argv[1]);
        openOutputFile(argv[2]);

        if ((ret = initFilters()) < 0) {
            throw std::runtime_error("init_filters failed");
        }
        if (!((packet = av_packet_alloc()))) {
            throw std::runtime_error("av_packet_alloc failed");
        }

        // read all packets
        while (true) {
            if ((ret = av_read_frame(ifmt_ctx, packet)) < 0) {
                break;
            }

            stream_index = packet->stream_index;
            av_log(nullptr, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n", stream_index);

            if (filter_ctx[stream_index].filter_graph) {
                auto stream = &stream_ctx[stream_index];

                av_log(nullptr, AV_LOG_DEBUG, "Going to reencode & filter the frame\n");

                ret = avcodec_send_packet(stream->dec_ctx, packet);
                if (ret < 0) {
                    av_log(nullptr, AV_LOG_ERROR, "Decoding failed\n");
                    break;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (ret < 0) {
                        throw runtime_error("avcodec_receive_frame returned error");
                    }

                    stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
                    if (stream->dec_frame->width && stream->dec_frame->height) {
                        drawOnTop(stream->dec_frame, stream->dec_ctx);
                    }

                    ret = filterEncodeWriteFrame(stream->dec_frame, stream_index);
                    if (ret < 0){
                        throw runtime_error("filter_encode_write_frame returned error");
                    }
                }
            } else {
                // remux this frame without reencoding
                av_packet_rescale_ts(packet,
                                     ifmt_ctx->streams[stream_index]->time_base,
                                     ofmt_ctx->streams[stream_index]->time_base);

                ret = av_interleaved_write_frame(ofmt_ctx, packet);
                if (ret < 0){
                    throw runtime_error("av_interleaved_write_frame returned error");
                }
            }
            av_packet_unref(packet);
        }

        // flush decoders, filters and encoders
        for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
            if (!filter_ctx[i].filter_graph) {
                continue;
            }

            auto stream = &stream_ctx[i];

            // flush decoder
            ret = avcodec_send_packet(stream->dec_ctx, nullptr);
            if (ret < 0) {
                throw runtime_error("Flushing decoding failed");
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
                if (ret == AVERROR_EOF) {
                    break;
                }

                if (ret < 0) {
                    throw runtime_error("avcodec_receive_frame returned error");
                }

                stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
                ret = filterEncodeWriteFrame(stream->dec_frame, i);
                if (ret < 0){
                    throw runtime_error("filter_encode_write_frame returned error");
                }
            }

            // flush filter
            ret = filterEncodeWriteFrame(nullptr, i);
            if (ret < 0) {
                throw runtime_error("Flushing filter failed");
            }

            // flush encoder
            ret = flushEncoder(i);
            if (ret < 0) {
                throw runtime_error("Flushing encoder failed\n");
            }
        }

        av_write_trailer(ofmt_ctx);
    } catch (runtime_error& e) {
        LOGE << e.what();
    }

    cleanUp(packet, ret);

    glfwDestroyWindow(window);
    glfwTerminate();

    decodeWatch.setEnd();
    LOG << "bombich! decode took: " << decodeWatch.getDt() << " ms";
}
