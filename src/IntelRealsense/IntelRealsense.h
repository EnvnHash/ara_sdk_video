//
// Created by user on 14.08.2021.
//

#define REALSENSE_USE_CHROMA 1


#if defined(ARA_USE_REALSENSE) && !defined(__ANDROID__)

#pragma once

#include <librealsense2/rs.hpp>

#include <AVCommon.h>

#include <Utils/PBO.h>
#include <Utils/Texture.h>
#include <GLBase.h>
#include <GLUtils/FastBlurMem.h>
#include <GLUtils/GLSLChromaKey.h>

#include <StopWatch.h>
#include <Conditional.h>
#include "ListProperty.h"

#ifdef REALSENSE_USE_CHROMA
#include "GLUtils/GLSLChromaKey.h"
#include "GLUtils/GLSLChromaKeyPar.h"
#endif

namespace ara::av::rs
{

class System;
class Device;

struct e_frame {
	Device*				dev{};
	void*				imgPtr{};
	uint16_t			pixSize[2]{};
	uint8_t				bytesPerPixel{};
};

struct rs2_resolution {
    int width{0};
    int height{0};
    int fps{0};
    rs2_format format{(rs2_format)0};
};

class Device {
public:

    Device(System* sys, const rs2::device& dev);
    virtual ~Device();

    std::string				getValue(rs2_camera_info info);

    std::string				getName(){return m_Name;}
    std::string				getSerial(){return m_Serial;}

    bool					start(int w, int h, int fps, int circ_frame_count, glb::GLBase* glbase, const std::function<void(e_frame* m_frame)> &img_cb);
    bool					stop();

    float					getDepthScale();
    rs2_stream				findStreamToAlign();
    void				    initMaskShader();
    void				    initTempFiltShader();
    void				    initApplyMaskShader();

    // color of clipped background
    void					setFillColor(int r, int g, int b);

    // clip the background, minimum, maximum distance in meters (all positive)
    void					setClip(float clip_min, float clip_max, float clipY);
    void					setClipMin(float clip_min) { m_Clip[0]=std::max<float>(std::min<float>(clip_min,m_Clip[1]),0.f); }
    void					setClipMax(float clip_max) { m_Clip[1]=std::max<float>(m_Clip[0],clip_max); }
    void					setClipYMin(float clip) { m_Clip[2] = clip; }

    void					glUpload();

    inline float			getClipMin()        { return m_Clip[0]; }
    inline float			getClipMax()        { return m_Clip[1]; }
    inline float			getClipY()          { return m_Clip[2]; }
    inline GLuint			glGetTexID()        { return m_resFbo ? m_resFbo->src->getColorImg() : 0; }
    inline glb::FBO*		getResFbo()         { return m_resFbo->src; }
    inline int				glGetWidth()        { return m_width; }
    inline int				glGetHeight()       { return m_height; }
    inline std::mutex*		getSwapMtx()        { return &m_fboSwapMtx; }

    inline const rs2::device& getRs2Device()    { return m_Device; }

	inline void				setHoleRemovalFilter(bool on_off)       { m_Filter_HoleRemovalActive = on_off;}
	inline void				setHoleRemovalFilterAlgo(int val)       { m_Filter_HoleRemoval.set_option(rs2_option::RS2_OPTION_HOLES_FILL, (float)val); }
	inline rs2::hole_filling_filter* getFilter_HoleRemoval()        { return &m_Filter_HoleRemoval;}
	inline bool				hasFilter_HoleRemoval()                 { return m_Filter_HoleRemovalActive;}

    inline void				setTemporalFilter(bool on_off)          { m_temporalFilterActive = on_off;}
    inline void             setTemporalFilterPersistency(float val) { m_temporalFilter.set_option(rs2_option::RS2_OPTION_HOLES_FILL, std::floor(val)); }; /// 0 -8
    inline void             setTemporalFilterSmoothAlpha(float val) { m_temporalFilter.set_option(rs2_option::RS2_OPTION_FILTER_SMOOTH_ALPHA, val); } /// 0-1
    inline void             setTemporalFilterSmoothDelta(float val) {
        try {
            m_temporalFilter.set_option(rs2_option::RS2_OPTION_FILTER_SMOOTH_DELTA,  std::floor(val));
        } catch(...) {
        }
    } /// 0-100

    inline void				setSpatFilter(bool on_off)              { m_spatFilterActive = on_off;}
    inline void             setSpatFilterMag(float val)             { m_spat_filter.set_option(rs2_option::RS2_OPTION_HOLES_FILL, std::floor(val)); };/// 1-5
    inline void             setSpatFilterSmoothAlpha(float val)     { m_spat_filter.set_option(rs2_option::RS2_OPTION_FILTER_SMOOTH_ALPHA, val); }/// 0.25-1
    inline void             setSpatFilterSmoothDelta(float val)     { m_spat_filter.set_option(rs2_option::RS2_OPTION_FILTER_SMOOTH_DELTA,  std::floor(val)); }
    inline void             setSpatFilterHoleFill(float val)        { m_spat_filter.set_option(rs2_option::RS2_OPTION_HOLES_FILL, std::floor(val)); } /// 0-5

    inline void             setGlTimeFiltOnOff(bool val)            { m_glTimeFiltActive = val; } /// 0-5
    inline void             setTimeFiltThres(int val)               { m_timeFiltThres = val; } /// 0-5
    inline void             setBlurNumIt(int blurIt)                { m_nrBlurIt = blurIt; }
    inline void             setBlurScale(float blurScale)           { m_blurScale = blurScale; }
    inline void             setTimeMedRange(int range)              { m_timeMedRange = std::min<int>(range, (int)m_firstPassBufSize); }
    inline void             setSmoothStepLow(float edge0)           { m_ss.x = std::min(edge0, m_ss.y); }
    inline void             setSmoothStepHigh(float edge1)          { m_ss.y = std::max(edge1, m_ss.x); }

    inline void             setRemoveBackground(bool val)           { m_removeBackground = val; }
	inline void             setForPointCloud(bool val)              { m_outputForPointCloud = val; }
	inline void             setConnectionLostCb(std::function<void()> f) { m_lostConnCb = std::move(f); }
	inline bool             isRunning()                             { return m_WorkerState == 2; }

#ifdef REALSENSE_USE_CHROMA
    inline void				readChromaHistoVal() { m_chroma.readHistoVal(); }
    inline void             setChromaProp(util::Property<glsg::GLSLChromaKeyPar*>* p) { m_chromaPar = p; }
#endif

    bool                    m_found=false;
    std::function<void()>   m_lostConnCb;

private:
    void					f_worker();
    bool					profileChanged();
    void					getStreamModes();
    void					calculateDepthLUT(float depth_scale);
    void					removeBackground(rs2::video_frame& img_frame, const rs2::depth_frame& depth_frame, float clip_min, float clip_max);
	bool					copyImage(e_frame* frame, rs2::video_frame& img_frame, const rs2::depth_frame& depth_frame, float clip_min, float clip_max);
    bool					allocCirc(size_t count);
    void					freeCirc();

    System*					            m_System{};
    rs2::device				            m_Device{};
    std::string				            m_Name{};
    std::string				            m_Serial{};

    rs2::pipeline			            m_Pipe;
    rs2::config				            m_Config;
    rs2::pipeline_profile	            m_Profile;

    float					            m_Clip[3]{0.f, 1.f, 0.f};
    float					            m_depthScale=0.001f;

    std::atomic<int>		            m_WorkerState=0; // 0=idle, 1 = starting. 2 = run, 3 = stopping
    std::vector<float>		            m_DepthLUT;
    std::vector<uint8_t>	            m_Fill;
    std::function<void(e_frame* m_frame)> m_ImageCB{};

    int                                 m_nrBlurIt=1;
    float                               m_blurScale=1.f;
    glm::vec2                           m_ss={0.f, 1.f};

    std::vector<e_frame>	            m_Circ;
    size_t					            m_CircCurrent{};

    glb::GLBase*                        m_glbase=nullptr;
    int                                 m_circ_frame_count=0;
    int                                 m_fps=30;

    std::vector<std::unique_ptr<glb::FBO>> m_firstPassFbos;
    size_t                              m_firstPassBufSize=4;
    size_t                              m_firstPassPtr=0;
    int                                 m_timeMedRange=4;

    std::unique_ptr<glb::FBO>           m_2ndPassFbo;
    std::unique_ptr<glb::PingPongFbo>   m_resFbo;
    glb::PBO                            m_pbo;
	std::unique_ptr<glb::Texture>       m_rgbTex;
    std::unique_ptr<glb::Texture>       m_depthTex;
    std::unique_ptr<glsg::FastBlurMem>  m_fastBlurMem;

	uint16_t				            m_glPixSize[2]{};
	void*					            m_glImagePtr{};

    rs2::frameset                       m_frameset;
    glb::Shaders*                       m_maskShdr=nullptr;
    glb::Shaders*                       m_applyMaskShdr=nullptr;
    glb::Shaders*                       m_tempFiltShdr=nullptr;

	bool					            m_Filter_HoleRemovalActive=false;
	rs2::hole_filling_filter            m_Filter_HoleRemoval;
    bool					            m_temporalFilterActive=false;
    rs2::temporal_filter                m_temporalFilter;
    bool					            m_spatFilterActive=false;
    rs2::spatial_filter                 m_spat_filter;    // Spatial    - edge-preserving spatial smoothing

	bool                                m_removeBackground=true;
	bool                                m_glTimeFiltActive=true;
	bool                                m_outputForPointCloud=false;
	int                                 m_timeFiltThres=4;
	float                               m_glResInited=false;

	int                                 m_width=0;
	int                                 m_height=0;
    util::StopWatch                     m_watch;
    util::Conditional                   m_stopCond;

    std::unordered_map<rs2_stream, util::ListProperty<rs2_resolution>> m_resolutions;

    std::atomic<bool>                   m_reconnected;
    std::mutex                          m_fboSwapMtx;

#ifdef REALSENSE_USE_CHROMA
    std::unique_ptr<glb::Quad>          m_quad;
    glb::Shaders*                       m_stdTex=nullptr;
    glsg::GLSLChromaKey                 m_chroma;
    util::Property<glsg::GLSLChromaKeyPar*>* m_chromaPar=nullptr;
#endif
};

class System {
public:

    bool					init();
    bool					enumerateDevices();
    void                    print_device_information(const rs2::device& dev);

    inline bool				isInited() { return m_inited; }
    inline size_t			getDeviceCount() {return m_Devices.size(); }
	inline Device*			getDevice(size_t index){return (index<0 || index>=getDeviceCount()) ? nullptr : m_Devices[index].get(); }
    inline std::vector<std::unique_ptr<Device>>& getDevices(){return m_Devices;}
    inline Device*          getFirstDevice() { return m_Devices.empty()  ? nullptr : m_Devices.front().get(); }
    inline void 			glIterate() { for (auto& d : m_Devices) d->glUpload(); }
    inline rs2::context&	getContext() { return m_Context; }

	void					getYUVLut(uint8_t* yuv_ptr[3]){for(int i=0; i<3; i++) yuv_ptr[i]=&m_YUVLut[i][0]; }
	void					RGB2YUV(uint8_t yuv[3], uint8_t r, uint8_t g, uint8_t b) {	size_t pos=r;pos<<=8;pos|=g;pos<<=8;pos|=b;	for (int i=0; i<3; i++) yuv[i]=m_YUVLut[i][pos];}

private:
	void					createYUVLut();

    bool                                    m_inited=false;
    rs2::context			                m_Context{};
    std::vector<std::unique_ptr<Device>>	m_Devices;
	std::vector<uint8_t>	                m_YUVLut[3];
};

}
#endif