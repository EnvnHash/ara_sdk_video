#if defined(ARA_USE_REALSENSE) && !defined(__ANDROID__)

#include <IntelRealsense/IntelRealsense.h>

using namespace std;
using namespace ara::glb;

namespace ara::av::rs
{

bool System::init() {

    if (m_inited) return true;

	createYUVLut();
    enumerateDevices();

    // listen for connection changes
    m_Context.set_devices_changed_callback([this](rs2::event_information& info)
    {
        for (auto &dev : m_Devices)
            dev->m_found = false;

        // check all registered devices for connection changes
        for (auto dev_new : info.get_new_devices())
        {
            auto new_id = dev_new.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
            for (auto &dev : m_Devices)
            {
                if (dev->getSerial() == new_id)
                    dev->m_found = true;
            }
        }

        // remove lost device from m_Devices
        for (auto it = m_Devices.begin(); it != m_Devices.end();)
        {
            if (!(*it)->m_found)
            {
                LOGE << "IntelRealsense disconnected!!";
                (*it)->stop();

                if ((*it)->m_lostConnCb)
                    (*it)->m_lostConnCb();

                it = m_Devices.erase(it);
            }
            else
                it++;
        }
    });

    m_inited = true;
    return true;
}

void System::createYUVLut() {

	m_YUVLut[0].resize(256*256*256);
	m_YUVLut[1].resize(256*256*256);
	m_YUVLut[2].resize(256*256*256);

	int r,g,b;
	uint8_t* yuv[3] = { &m_YUVLut[0][0],&m_YUVLut[1][0],&m_YUVLut[2][0] };

	for (r=0; r<256; r++)
		for (g = 0; g < 256; g++)
		{
			for (b = 0; b < 256; b++)
			{
				yuv[0][b] = (uint8_t)(std::clamp<double>( 0.257 * r + 0.504 * g + 0.098 * b +  16,0.,255.));
				yuv[1][b] = (uint8_t)(std::clamp<double>(-0.148 * r - 0.291 * g + 0.439 * b + 128,0.,255.));
				yuv[2][b] = (uint8_t)(std::clamp<double>( 0.439 * r - 0.368 * g - 0.071 * b + 128,0.,255.));
			}

			yuv[0]+=256;
			yuv[1]+=256;
			yuv[2]+=256;
		}
}

bool System::enumerateDevices()
{
    auto devices = m_Context.query_devices();
    if (devices.size() == 0)
        return false;

    for (rs2::device device : devices)
        m_Devices.emplace_back(std::make_unique<Device>(this,device));

    return true;
}

void System::print_device_information(const rs2::device& dev)
{
    // Each device provides some information on itself
    // The different types of available information are represented using the "RS2_CAMERA_INFO_*" enum

    LOG << "Device information: ";
    //The following code shows how to enumerate all of the RS2_CAMERA_INFO
    //Note that all enum types in the SDK start with the value of zero and end at the "*_COUNT" value
    for (int i = 0; i < static_cast<int>(RS2_CAMERA_INFO_COUNT); i++)
    {
        rs2_camera_info info_type = static_cast<rs2_camera_info>(i);
        //SDK enum types can be streamed to get a string that represents them
        std::cout << "  " << std::left << std::setw(20) << info_type << " : ";

        //A device might not support all types of RS2_CAMERA_INFO.
        //To prevent throwing exceptions from the "get_info" method we first check if the device supports this type of info
        if (dev.supports(info_type))
            std::cout << dev.get_info(info_type) << std::endl;
        else
            std::cout << "N/A" << std::endl;
    }
}

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

Device::Device(System* sys, const rs2::device& dev)
{
    m_System=sys;
    m_Device=dev;
    getStreamModes();

    m_Name=getValue(RS2_CAMERA_INFO_NAME);
    m_Serial=getValue(RS2_CAMERA_INFO_SERIAL_NUMBER);

    m_Fill.resize(4096<<2);
    setFillColor(0,255,0);
}

Device::~Device()
{
    freeCirc();
}

std::string Device::getValue(rs2_camera_info info)
{
    return m_Device.supports(info) ? m_Device.get_info(info) : "";
}

bool Device::start(int w, int h, int fps, int circ_frame_count, glb::GLBase* glbase, const std::function<void(e_frame* m_frame)> &img_cb) {

    if (m_WorkerState!=0)
        return false;

    m_width = w;
    m_height = h;
    m_fps = fps;
    m_glbase = glbase;

    m_WorkerState=1;
    m_ImageCB=img_cb;
    m_circ_frame_count = circ_frame_count;

    std::thread(&Device::f_worker,this).detach();

    return true;
}

bool Device::stop()
{
    if (m_WorkerState < 2)
        return false;

    m_WorkerState=3;

    m_Pipe.stop();
    m_stopCond.wait(0);

    return true;
}

void Device::f_worker() {

    allocCirc(m_circ_frame_count);

    m_Config.enable_stream(RS2_STREAM_COLOR, m_width, m_height, RS2_FORMAT_RGB8, m_fps);
    m_Config.enable_stream(RS2_STREAM_DEPTH, m_width, m_height, RS2_FORMAT_Z16, m_fps); // 1280 x 720 works only with 6 fps
    m_Config.enable_device(m_Serial);

    // register in m_System
    try {
        m_Profile = m_Pipe.start(m_Config);
        if (!m_Profile)
            LOGE << "IntelRealsense Device::start failed";
    }
    catch (...) {
        if (!m_Profile)
            LOGE << "IntelRealsense Device::start failed";
        return;
    }

#ifndef __APPLE__
    m_depthScale = getDepthScale(); // hier crash unter macos catalina
    calculateDepthLUT(m_depthScale);
#endif
//    rs2::align align_to_color(RS2_STREAM_COLOR);
    rs2_stream align_to = findStreamToAlign();
    rs2::align align(align_to);

    int w,h,bpp;
	void* srcptr;

    m_WorkerState=2;
    m_CircCurrent=0;

    while (m_WorkerState==2)
    {
        try {
            m_frameset = m_Pipe.wait_for_frames();
        } catch (rs2::error e){
            LOGE << "IntelRealsense::Error m_Pipe.wait_for_frames(); error " << e.what();
            continue;
        }

        if (m_WorkerState!=2) break; // double check, loop can be stopped asynchronously

        if (profileChanged())
        {
            m_Profile = m_Pipe.get_active_profile();
            align_to = findStreamToAlign();
            align = rs2::align(align_to);
            m_depthScale = getDepthScale();
            calculateDepthLUT(getDepthScale());
        }

        m_frameset = align.process(m_frameset);

        auto aligned_depth_frame = m_frameset.get_depth_frame();
        auto img_frame = m_frameset.get_color_frame();

        if (m_spatFilterActive)
            aligned_depth_frame = m_spat_filter.process(aligned_depth_frame);

        if (m_temporalFilterActive){
            setTemporalFilterPersistency(3.f);
            aligned_depth_frame = m_temporalFilter.process(aligned_depth_frame);
        }

		if (m_Filter_HoleRemovalActive){
            setHoleRemovalFilterAlgo(1); // 1 looks best...
			aligned_depth_frame = m_Filter_HoleRemoval.process(aligned_depth_frame);
        }

        if (m_WorkerState!=2) break;

        if (aligned_depth_frame && img_frame)
		{
			if (   (w=img_frame.get_width()) > 0
				&& (h=img_frame.get_height()) > 0 
				&& (bpp=img_frame.get_bytes_per_pixel()) > 0 
				&& (srcptr=(void*)img_frame.get_data())!=nullptr)
			{
				if (!m_Circ.empty())
				{
					e_frame* fr = &m_Circ[m_CircCurrent % m_Circ.size()];

                    // set image info
					if (fr->pixSize[0] != w || fr->pixSize[1] != h || fr->bytesPerPixel != bpp)
					{
						if (fr->imgPtr != nullptr) free(fr->imgPtr);

						if ((fr->imgPtr = malloc(w * h * bpp)) != nullptr)
						{
							fr->pixSize[0] = w;
							fr->pixSize[1] = h;
							fr->bytesPerPixel = bpp;
						}
						else
						{
							fr->pixSize[0] = fr->pixSize[1] = 0;
							fr->bytesPerPixel = 0;
							fr = nullptr;
						}
					}

                    // do we have a valid frame?
					if (fr)
					{
						if (copyImage(fr, img_frame, aligned_depth_frame, m_Clip[0], m_Clip[1]))
						{
							fr->dev = this;
							if (m_ImageCB) m_ImageCB(fr);
							m_CircCurrent++;
						}
					}
				}
				else
				{
				    // only called if m_Circ is empty
					if (m_removeBackground)
					    removeBackground(img_frame, aligned_depth_frame, m_Clip[0], m_Clip[1]);

					if (m_ImageCB)
					{
						e_frame fr;

						fr.dev = this;
						fr.pixSize[0] = w;
						fr.pixSize[1] = h;
						fr.bytesPerPixel = bpp;
						fr.imgPtr = srcptr;

						m_ImageCB(&fr);
					}
				}
			}
        }
     }

    m_WorkerState=0;

    m_stopCond.notify();
}

float Device::getDepthScale()
{
    for (rs2::sensor& sensor : m_Device.query_sensors())
        if (rs2::depth_sensor dpt = sensor.as<rs2::depth_sensor>()) {
            return dpt.get_depth_scale();
        }

        return 0.f;
}

rs2_stream Device::findStreamToAlign()
{
    rs2_stream align_to = RS2_STREAM_ANY;

    bool depth_stream_found = false;
    bool color_stream_found = false;

    for (rs2::stream_profile sp : m_Profile.get_streams())
    {
        rs2_stream profile_stream = sp.stream_type();

        if (profile_stream != RS2_STREAM_DEPTH)
        {
            if (!color_stream_found)
                align_to = profile_stream;

            if (profile_stream == RS2_STREAM_COLOR)
                color_stream_found = true;
        }
        else depth_stream_found = true;
    }

    return !depth_stream_found ? RS2_STREAM_ANY : align_to;
}

bool Device::profileChanged()
{
    const std::vector<rs2::stream_profile> current=m_Pipe.get_active_profile().get_streams();
    const std::vector<rs2::stream_profile> prev=m_Profile.get_streams();

    for (auto&& sp : prev)
    {
        auto itr = std::find_if(std::begin(current), std::end(current), [&sp](const rs2::stream_profile& current_sp) { return sp.unique_id() == current_sp.unique_id(); });
        if (itr == std::end(current)) return true;
    }

    return false;
}

void Device::getStreamModes()
{
    m_resolutions[RS2_STREAM_DEPTH].clear();
    m_resolutions[RS2_STREAM_COLOR].clear();

    for (auto&& sensor : m_Device.query_sensors())
    {
        std::string sensorName(sensor.get_info(RS2_CAMERA_INFO_NAME));

         //cout << "Stream Profiles supported by " << sensorName << endl;
         //cout << " Supported modes:\n" << setw(16) << "    stream" << setw(16) << " resolution" << setw(10) << " fps" << setw(10) << " format" << endl;

        // Show which streams are supported by this device
        for (auto&& profile : sensor.get_stream_profiles())
        {
            if (auto video = profile.as<rs2::video_stream_profile>())
            {
                //cout << " #   " << profile.stream_name() << "\t  " << video.width() << "x" << video.height() << "\t@ " << profile.fps() << setw(6) << "Hz\t" << profile.format() << endl;
                if (sensorName == "Stereo Module" && profile.stream_name() == "Depth")
                {
                    m_resolutions[RS2_STREAM_DEPTH].emplace_back(rs2_resolution{video.width(), video.height(), profile.fps(), profile.format() }, false);
                } else if (sensorName == "RGB Camera" && profile.stream_name() == "Color" && profile.format() == RS2_FORMAT_RGB8)
                {
                    m_resolutions[RS2_STREAM_COLOR].emplace_back(rs2_resolution{video.width(), video.height(), profile.fps(), profile.format() }, false);
                    //cout << " #   " << profile.stream_name() << "\t  " << video.width() << "x" << video.height() << "\t@ " << profile.fps() << setw(6) << "Hz\t" << profile.format() << endl;
                }
            }
        }

        //cout << endl;
    }

    m_resolutions[RS2_STREAM_COLOR].procCb();
    m_resolutions[RS2_STREAM_COLOR].procCb();
}

void Device::removeBackground(rs2::video_frame& img_frame, const rs2::depth_frame& depth_frame, float clip_min, float clip_max)
{
    const uint16_t* depth = reinterpret_cast<const uint16_t*>(depth_frame.get_data());
    uint8_t* m_frame = reinterpret_cast<uint8_t*>(const_cast<void*>(img_frame.get_data()));
    const float* dlut=&m_DepthLUT[0];

    int w = img_frame.get_width();
    int h = img_frame.get_height();
    int BPP = img_frame.get_bytes_per_pixel();
    float pd;
    int lpos=0,x,y;
    bool inout;
    uint8_t* fill=&m_Fill[0];

    for (y = 0; y < h; y++, depth += w, m_frame += w * BPP) {
        for (x = 0, inout=true; x < w; x++) {
            if ((pd = dlut[depth[x]]) >= clip_min && pd < clip_max) {

                if (!inout) {
                    std::memcpy(&m_frame[lpos*BPP], &fill[lpos*BPP], BPP*(x-lpos));
                    inout=true;
                }
            }
            else {
                if (inout) {
                    lpos=x;
                    inout=false;
                }
            }
        }

        if (!inout) std::memcpy(&m_frame[lpos*BPP], &fill[lpos*BPP], BPP*(x-lpos));
    }
}

bool Device::copyImage(e_frame* frame, rs2::video_frame& img_frame, const rs2::depth_frame& depth_frame,
                       float clip_min, float clip_max)
{
   // m_watch.setStart();

    m_glbase->addGlCbSync([this, &img_frame, &frame, &depth_frame]{

        auto rgb = reinterpret_cast<const uint8_t*>(img_frame.get_data());
        auto dest = reinterpret_cast<uint8_t*>(frame->imgPtr);
        auto depth = reinterpret_cast<const uint16_t*>(depth_frame.get_data());

        int w = img_frame.get_width();
        int h = img_frame.get_height();
        int BPP = img_frame.get_bytes_per_pixel();

        if (!m_glResInited)
        {
            m_rgbTex = make_unique<glb::Texture>(m_glbase);
            m_rgbTex->allocate2D(w, h, GL_RGB8, GL_RGB, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);

            m_depthTex = make_unique<glb::Texture>(m_glbase);
            m_depthTex->allocate2D(w, h, GL_R16, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_SHORT);

            for (int i=0; i<m_firstPassBufSize; i++)
                m_firstPassFbos.emplace_back(make_unique<FBO>(m_glbase, w/2, h/2, GL_R8, GL_TEXTURE_2D, false, 1, 1, 1, GL_CLAMP_TO_EDGE, false));

            m_2ndPassFbo = make_unique<FBO>(m_glbase, w/2, h/2, GL_RGBA8, GL_TEXTURE_2D, false, 1, 1, 1, GL_CLAMP_TO_EDGE, false);

            m_resFbo = make_unique<PingPongFbo>(m_glbase, w, h, GL_RGBA8, GL_TEXTURE_2D, false, 1, 1, 1, GL_CLAMP_TO_EDGE, false);
            m_fastBlurMem = make_unique<glsg::FastBlurMem>(m_glbase, 0.3f, w, h);

            m_stdTex = m_glbase->shaderCollector().getStdTex();

            m_quad = make_unique<Quad>(-1.f, -1.f, 2.f, 2.f,
                                       glm::vec3(0.f, 0.f, 1.f),
                                       0.f, 0.f, 0.f, 1.f,
                                       nullptr, 1, false);  // create a Quad, standard width and height (normalized into -1|1), static red

            initMaskShader();
            initTempFiltShader();
            initApplyMaskShader();

            m_glResInited = true;
        }

        m_rgbTex->upload((void*)rgb);
        m_depthTex->upload((void*)depth);

        // first pass, clip by distance and y, generate a b/w mask
        if (m_maskShdr)
        {
            m_firstPassFbos[m_firstPassPtr]->bind();

            m_maskShdr->begin();
            m_maskShdr->setIdentMatrix4fv("m_pvm");
            m_maskShdr->setUniform1i("depth", 0);
            m_maskShdr->setUniform1f("depthScale", m_depthScale);
            m_maskShdr->setUniform3fv("clip", &m_Clip[0]);
            m_maskShdr->setUniform2f("xzYzFactor", (float)(std::tan(0.75) * 2.0), (float)(std::tan(0.497) * 2.0)); // half hor FOV intel realsense d435

            m_depthTex->bind(0);

            glBindVertexArray(*m_glbase->nullVao());
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            m_firstPassFbos[m_firstPassPtr]->unbind();

            m_firstPassPtr = ++m_firstPassPtr % m_firstPassBufSize;
        }

        // second pass, temporal filter
        if (m_tempFiltShdr)
        {
            m_2ndPassFbo->bind();

            m_tempFiltShdr->begin();
            m_tempFiltShdr->setIdentMatrix4fv("m_pvm");
            m_tempFiltShdr->setUniform1i("depth", 0);
            m_tempFiltShdr->setUniform1i("depthMinOne", 1);
            m_tempFiltShdr->setUniform1i("depthMinTwo", 2);
            m_tempFiltShdr->setUniform1i("depthMinThree", 3);
            m_tempFiltShdr->setUniform1i("timeRange", m_timeMedRange);
            m_tempFiltShdr->setUniform1i("medThres", m_timeFiltThres);
            m_tempFiltShdr->setUniform1i("filtActive", (int)m_glTimeFiltActive);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_firstPassFbos[(m_firstPassPtr -1 +m_firstPassBufSize) % m_firstPassBufSize]->getColorImg());

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_firstPassFbos[(m_firstPassPtr -2 +m_firstPassBufSize) % m_firstPassBufSize]->getColorImg());

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, m_firstPassFbos[(m_firstPassPtr -3 +m_firstPassBufSize) % m_firstPassBufSize]->getColorImg());

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, m_firstPassFbos[(m_firstPassPtr -4 +m_firstPassBufSize) % m_firstPassBufSize]->getColorImg());

            glBindVertexArray(*m_glbase->nullVao());
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            m_2ndPassFbo->unbind();
        }

        // third pass blur
        m_fastBlurMem->setBright(1.f);
        m_fastBlurMem->setOffsScale((float)m_blurScale);
        m_fastBlurMem->proc(m_2ndPassFbo->getColorImg());

        for (int i=0;i<m_nrBlurIt;i++)
            m_fastBlurMem->proc(m_fastBlurMem->getResult());

        // fourth pass apply mask
        m_resFbo->dst->bind();

        m_applyMaskShdr->begin();
        m_applyMaskShdr->setIdentMatrix4fv("m_pvm");
        m_applyMaskShdr->setUniform1i("rgb", 0);
        m_applyMaskShdr->setUniform1i("depth", 1);
        m_applyMaskShdr->setUniform2fv("ss", &m_ss[0]);

        m_rgbTex->bind(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_fastBlurMem->getResult());

        glBindVertexArray(*m_glbase->nullVao());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        m_resFbo->dst->unbind();

        // optional chroma key
#ifdef REALSENSE_USE_CHROMA
        if (m_chromaPar && !m_chroma.isInited())
            m_chroma.init(m_glbase, m_chromaPar);

        auto res = m_chroma.proc(m_resFbo->dst->getColorImg());
        if (res) {
            m_resFbo->dst->bind();
            m_resFbo->dst->clear();

            m_stdTex->begin();
            m_stdTex->setIdentMatrix4fv("m_pvm");
            m_stdTex->setUniform1i("tex", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, res);

            m_quad->draw();

            m_resFbo->dst->unbind();
        }

#endif
        unique_lock<mutex> l(m_fboSwapMtx);
        m_resFbo->swap();

        glFinish();

        return true;
    });

  //  m_watch.setEnd();
  //  m_watch.print("rs ");

    return true;
}

void Device::initMaskShader()
{
    if (!m_glbase) return;

    std::string vert = STRINGIFY(
        uniform mat4 m_pvm;
        out vec2 tex_coord;
        const vec2[4] quadVertices = vec2[4](vec2(0.,0.),vec2(1.,0.),vec2(0.,1.),vec2(1.,1.));\n
        void main() {
            tex_coord = quadVertices[gl_VertexID];\n
            gl_Position = m_pvm * vec4(quadVertices[gl_VertexID] * 2.0 - 1.0, 0.0, 1.0);\n
        });
    vert = "// Realsense Mask, vert\n" + m_glbase->shaderCollector().getShaderHeader() + vert;

    std::string frag = STRINGIFY(in vec2 tex_coord;\n
        layout(location = 0) out vec4 fragColor;\n
        uniform sampler2D depth;\n
        uniform vec2 xzYzFactor;\n
        uniform vec3 clip;\n
        uniform float depthScale;\n
        \n
        // normTex [-0.5 | 0.5]
        vec3 getRealWorldCoord(float x, float y, float dp) {\n
            return vec3(x * 0.5 * dp * xzYzFactor.x,\n
                        y * 0.5 * dp * xzYzFactor.y,\n
                        dp); \n
        } \n
        float getDepth(vec2 tc) {
             float dpt = texture(depth, tc).r * 65536.0 * depthScale;\n
             vec3 rw = getRealWorldCoord(tc.x - 0.5, tc.y - 0.5, dpt);\n
             return dpt > clip.x && dpt < clip.y && -rw.y > clip.z ? 1.0 : 0.0;
        } \n
        void main() {\n
            fragColor = vec4( getDepth(tex_coord) );\n
        });

    frag = "// Realsense Mask, frag\n" + m_glbase->shaderCollector().getShaderHeader() + frag;

    m_maskShdr = m_glbase->shaderCollector().add("RealsenseMask", vert, frag);
}

void Device::initTempFiltShader()
{
    if (!m_glbase) return;

    std::string vert = STRINGIFY(
        uniform mat4 m_pvm;
        out vec2 tex_coord;
        const vec2[4] quadVertices = vec2[4](vec2(0.,0.),vec2(1.,0.),vec2(0.,1.),vec2(1.,1.));\n
        void main() {
            tex_coord = quadVertices[gl_VertexID];\n
            gl_Position = m_pvm * vec4(quadVertices[gl_VertexID] * 2.0 - 1.0, 0.0, 1.0);\n
    });
    vert = "// Realsense Temp Filt, vert\n" + m_glbase->shaderCollector().getShaderHeader() + vert;

    std::string frag = STRINGIFY(in vec2 tex_coord;\n
        layout(location = 0) out vec4 fragColor;\n
        uniform sampler2D depth;\n
        uniform sampler2D depthMinOne;\n
        uniform sampler2D depthMinTwo;\n
        uniform sampler2D depthMinThree;\n
        uniform int medThres;\n
        uniform int filtActive;\n
        uniform int timeRange;\n
        \n
        void main() {\n
            bool act = bool(filtActive);
            \n
            float actDpt = texture(depth, tex_coord).r;\n
            float dptMinOne = act && timeRange >= 2 ? texture(depthMinOne, tex_coord).r : 0.0;\n
            float dptMinTwo = act && timeRange >= 3 ? texture(depthMinTwo, tex_coord).r : 0.0;\n
            float dptMinThree = act && timeRange >= 4 ? texture(depthMinThree, tex_coord).r : 0.0;\n
            \n
            float filtDpt = act ? float(int(actDpt + dptMinOne + dptMinTwo + dptMinThree) >= min(timeRange, medThres)) : actDpt;
            \n
            fragColor = vec4(vec3(filtDpt), 1.0); \n
        });

    frag = "// Realsense Temp Filt, frag\n" + m_glbase->shaderCollector().getShaderHeader() + frag;

    m_tempFiltShdr = m_glbase->shaderCollector().add("RealsenseTempFilt", vert, frag);
}

void Device::initApplyMaskShader()
{
    if (!m_glbase) return;

    std::string vert = STRINGIFY(
        uniform mat4 m_pvm;
        out vec2 tex_coord;
        const vec2[4] quadVertices = vec2[4](vec2(0.,0.),vec2(1.,0.),vec2(0.,1.),vec2(1.,1.));\n
        void main() {
            tex_coord = quadVertices[gl_VertexID];\n
            gl_Position = m_pvm * vec4(quadVertices[gl_VertexID] * 2.0 - 1.0, 0.0, 1.0);\n
    });
    vert = "// Realsense apply Mask, vert\n" + m_glbase->shaderCollector().getShaderHeader() + vert;

    std::string frag = STRINGIFY(in vec2 tex_coord;\n
        layout(location = 0) out vec4 fragColor;\n
        uniform sampler2D rgb;\n
        uniform sampler2D depth;\n
        uniform vec2 ss;\n
        \n
        void main() {\n
            vec4 texCol = texture(rgb, tex_coord);
            float dpt = texture(depth, tex_coord).r;\n
            dpt = smoothstep(ss.x, ss.y, dpt);
            fragColor = vec4(texCol.rgb * dpt, 1.0);\n
        });

    frag = "// Realsense apply Mask, frag\n" + m_glbase->shaderCollector().getShaderHeader() + frag;

    m_applyMaskShdr = m_glbase->shaderCollector().add("RealsenseApplyMask", vert, frag);
}

void Device::calculateDepthLUT(float depth_scale)
{
    m_DepthLUT.resize(1<<16);

    for (int i = 0; i < 1 << 16; i++)
        m_DepthLUT[i]=float(i*depth_scale);
}

void Device::setFillColor(int r, int g, int b)
{
    uint8_t* v=&m_Fill[0];
    uint8_t c[3]{(uint8_t) r,(uint8_t) g,(uint8_t) b};
    size_t w=m_Fill.size()>>2;

    for (size_t i=0; i<w; i++){v[i*3+0]=c[0];v[i*3+1]=c[1];v[i*3+2]=c[2];}
}

void Device::setClip(float clip_min, float clip_max, float clipY)
{
    m_Clip[0]=std::max<float>(std::min<float>(clip_min,clip_max),0.f);
    m_Clip[1]=std::max<float>(m_Clip[0],clip_max);
    m_Clip[2]=clipY;
}

bool Device::allocCirc(size_t count)
{
    freeCirc();
    if (count<=0) return false;
    m_Circ.resize(count, e_frame{});
    return true;
}

void Device::freeCirc() {

	for (auto& c : m_Circ)
    {
		if (c.imgPtr != nullptr)
            free(c.imgPtr);

	}

	m_Circ.clear();

}

void Device::glUpload()
{
    if (m_glImagePtr != nullptr)
    {
        if (!m_pbo.isInited())
        {
            m_pbo.setSize(m_glPixSize[0], m_glPixSize[1]);
            m_pbo.setFormat(GL_RGB8);
            m_pbo.init();

     //       glGenTextures(1, &m_glTex);
       //     glBindTexture(GL_TEXTURE_2D, m_glTex);
         //   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB8, (GLsizei) m_glPixSize[0], (GLsizei) m_glPixSize[1]);
        }

        //m_pbo.upload(m_glTex, m_glImagePtr);

		m_glImagePtr=nullptr;
    }
}

}
#endif