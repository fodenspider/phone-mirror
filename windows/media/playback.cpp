#include "playback.hpp"
#include "phone_mirror/capture.hpp"
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace pm {
namespace {
void require(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream text; text << operation << " failed (0x" << std::hex << std::uint32_t(hr) << ")";
        throw std::runtime_error(text.str());
    }
}
struct Runtime {
    Runtime() {
        require(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM initialization");
        const auto hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) { CoUninitialize(); require(hr,"Media Foundation initialization"); }
    }
    ~Runtime() { MFShutdown(); CoUninitialize(); }
};
struct Waiter {
    std::mutex mutex; std::condition_variable_any changed;
    void until(std::stop_token stop, std::chrono::steady_clock::time_point when) {
        std::unique_lock lock(mutex);
        changed.wait_until(lock, stop, when, [] { return false; });
    }
};

class Renderer {
public:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Renderer(HWND hwnd, unsigned w, unsigned h) : hwnd_(hwnd), width_(w), height_(h) {
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "D3D11 device");
        ComPtr<ID3D10Multithread> protection;
        require(context.As(&protection), "D3D multithread interface");
        protection->SetMultithreadProtected(TRUE);
        require(device.As(&video_device_), "D3D11 video device");
        require(context.As(&video_context_), "D3D11 video context");
        if (hwnd_) {
            ComPtr<IDXGIDevice> dxgi; require(device.As(&dxgi), "DXGI device");
            ComPtr<IDXGIAdapter> adapter; require(dxgi->GetAdapter(&adapter), "DXGI adapter");
            ComPtr<IDXGIFactory2> factory; require(adapter->GetParent(IID_PPV_ARGS(&factory)), "DXGI factory");
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = w; desc.Height = h; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            require(factory->CreateSwapChainForHwnd(device.Get(), hwnd_, &desc, nullptr, nullptr, &swap_), "swap chain");
            factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
            ComPtr<IDXGIDevice1> device1;
            if (SUCCEEDED(dxgi.As(&device1))) device1->SetMaximumFrameLatency(1);
        }
    }
    bool resized() const {
        if (!hwnd_) return false;
        RECT r{}; GetClientRect(hwnd_, &r);
        return r.right > 0 && r.bottom > 0 && (unsigned(r.right) != target_width_ || unsigned(r.bottom) != target_height_);
    }
    bool render(IMFSample* sample, IMFMediaType* media_type) {
        ComPtr<IMFMediaBuffer> buffer; require(sample->GetBufferByIndex(0,&buffer), "decoded buffer");
        ComPtr<IMFDXGIBuffer> dxgi_buffer;
        ComPtr<ID3D11Texture2D> source; UINT subresource = 0;
        const bool gpu = SUCCEEDED(buffer.As(&dxgi_buffer));
        if (gpu) {
            require(dxgi_buffer->GetResource(IID_PPV_ARGS(&source)), "decoded GPU texture");
            require(dxgi_buffer->GetSubresourceIndex(&subresource), "decoded subresource");
        } else {
            UINT32 w=0,h=0; require(MFGetAttributeSize(media_type,MF_MT_FRAME_SIZE,&w,&h), "decoded dimensions");
            if (w < width_ || h < height_ || w > 4096 || h > 4096 || (w&1) || (h&1))
                throw std::runtime_error("unsupported CPU output dimensions");
            const auto size = std::size_t(w)*h*3/2;
            std::vector<BYTE> pixels(size);
            ComPtr<IMF2DBuffer> two_d;
            if (SUCCEEDED(buffer.As(&two_d))) {
                require(two_d->ContiguousCopyTo(pixels.data(), static_cast<DWORD>(pixels.size())), "copy NV12 planes");
            } else {
                UINT32 stride = w; media_type->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride);
                if (stride != w) throw std::runtime_error("padded linear NV12 output requires IMF2DBuffer");
                BYTE* data = nullptr; DWORD length = 0;
                require(buffer->Lock(&data,nullptr,&length),"lock CPU frame");
                if (length < size) { buffer->Unlock(); throw std::runtime_error("short NV12 output"); }
                std::memcpy(pixels.data(),data,size); buffer->Unlock();
            }
            if (!upload_ || upload_width_ != w || upload_height_ != h) {
                upload_.Reset();
                D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
                desc.Format=DXGI_FORMAT_NV12; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
                // Video-processor input accepts bind=0; SHADER_RESOURCE alone is invalid.
                desc.BindFlags=0;
                require(device->CreateTexture2D(&desc,nullptr,&upload_),"CPU fallback upload texture");
                upload_width_=w; upload_height_=h;
            }
            context->UpdateSubresource(upload_.Get(),0,nullptr,pixels.data(),w,static_cast<UINT>(size));
            source=upload_;
        }
        D3D11_TEXTURE2D_DESC src{}; source->GetDesc(&src);
        if (src.Format != DXGI_FORMAT_NV12 || src.Width < width_ || src.Height < height_)
            throw std::runtime_error("unexpected decoder surface format/dimensions");
        unsigned tw=width_,th=height_;
        if (hwnd_) {
            RECT r{}; GetClientRect(hwnd_, &r);
            if (r.right<=0 || r.bottom<=0) return gpu;
            tw=static_cast<unsigned>(r.right); th=static_cast<unsigned>(r.bottom);
        }
        if (!target_ || tw != target_width_ || th != target_height_) make_target(tw,th);
        if (!processor_ || processor_width_ != src.Width || processor_height_ != src.Height) {
            processor_.Reset(); enumerator_.Reset();
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
            desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            desc.InputWidth=src.Width; desc.InputHeight=src.Height;
            desc.OutputWidth=tw; desc.OutputHeight=th;
            desc.InputFrameRate={30,1}; desc.OutputFrameRate={30,1};
            desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            require(video_device_->CreateVideoProcessorEnumerator(&desc,&enumerator_),"video processor enumeration");
            require(video_device_->CreateVideoProcessor(enumerator_.Get(),0,&processor_),"video processor");
            processor_width_=src.Width; processor_height_=src.Height;
        }
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
        input_desc.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_desc.Texture2D.MipSlice=subresource % src.MipLevels;
        input_desc.Texture2D.ArraySlice=subresource / src.MipLevels;
        ComPtr<ID3D11VideoProcessorInputView> input;
        require(video_device_->CreateVideoProcessorInputView(source.Get(),enumerator_.Get(),&input_desc,&input),"video input view");
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
        output_desc.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> output;
        require(video_device_->CreateVideoProcessorOutputView(target_.Get(),enumerator_.Get(),&output_desc,&output),"video output view");
        const float black[4]={0,0,0,1};
        ComPtr<ID3D11RenderTargetView> rtv;
        require(device->CreateRenderTargetView(target_.Get(),nullptr,&rtv),"render target view");
        context->ClearRenderTargetView(rtv.Get(),black);
        RECT source_rect{0,0,static_cast<LONG>(width_),static_cast<LONG>(height_)};
        const double scale=std::min(double(tw)/width_,double(th)/height_);
        const LONG dw=static_cast<LONG>(width_*scale),dh=static_cast<LONG>(height_*scale);
        RECT dest{(LONG(tw)-dw)/2,(LONG(th)-dh)/2,(LONG(tw)+dw)/2,(LONG(th)+dh)/2};
        RECT target_rect{0,0,LONG(tw),LONG(th)};
        video_context_->VideoProcessorSetStreamFrameFormat(processor_.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        video_context_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(),0,FALSE);
        video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(),0,TRUE,&source_rect);
        video_context_->VideoProcessorSetStreamDestRect(processor_.Get(),0,TRUE,&dest);
        video_context_->VideoProcessorSetOutputTargetRect(processor_.Get(),TRUE,&target_rect);
        UINT32 matrix=height_>=720?MFVideoTransferMatrix_BT709:MFVideoTransferMatrix_BT601;
        UINT32 range=MFNominalRange_16_235;
        media_type->GetUINT32(MF_MT_YUV_MATRIX,&matrix); media_type->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,&range);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE in_color{};
        in_color.YCbCr_Matrix=matrix==MFVideoTransferMatrix_BT709;
        in_color.Nominal_Range=range==MFNominalRange_0_255?2:1;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE out_color{};
        video_context_->VideoProcessorSetStreamColorSpace(processor_.Get(),0,&in_color);
        video_context_->VideoProcessorSetOutputColorSpace(processor_.Get(),&out_color);
        D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable=TRUE; stream.pInputSurface=input.Get();
        require(video_context_->VideoProcessorBlt(processor_.Get(),output.Get(),0,1,&stream),"video conversion/render");
        if (swap_) { require(swap_->Present(1,0),"present"); target_.Reset(); }
        return gpu;
    }
    void snapshot(const std::filesystem::path& path) {
        if (!target_) throw std::runtime_error("snapshot requires offscreen target");
        D3D11_TEXTURE2D_DESC desc{}; target_->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        require(device->CreateTexture2D(&desc,nullptr,&staging),"snapshot staging texture");
        context->CopyResource(staging.Get(),target_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"snapshot readback");
        std::vector<BYTE> image(std::size_t(desc.Width)*desc.Height*4);
        for (UINT y=0;y<desc.Height;++y)
            std::memcpy(image.data()+std::size_t(y)*desc.Width*4,static_cast<BYTE*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,desc.Width*4);
        context->Unmap(staging.Get(),0);
        BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
        file.bfSize=file.bfOffBits+static_cast<DWORD>(image.size());
        BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=desc.Width;
        info.biHeight=-static_cast<LONG>(desc.Height); info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB;
        std::ofstream output(path,std::ios::binary); if (!output) throw std::runtime_error("cannot write snapshot");
        output.write(reinterpret_cast<char*>(&file),sizeof(file)); output.write(reinterpret_cast<char*>(&info),sizeof(info));
        output.write(reinterpret_cast<char*>(image.data()),image.size());
        if (!output) throw std::runtime_error("snapshot write failed");
    }
private:
    void make_target(unsigned w,unsigned h) {
        target_.Reset();
        if (swap_) {
            if (w!=target_width_ || h!=target_height_) {
                require(swap_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"resize swap chain");
                processor_.Reset();
            }
            require(swap_->GetBuffer(0,IID_PPV_ARGS(&target_)),"swap chain buffer");
        } else {
            D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
            desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
            require(device->CreateTexture2D(&desc,nullptr,&target_),"offscreen target");
        }
        target_width_=w; target_height_=h;
    }
    HWND hwnd_; unsigned width_,height_,target_width_=0,target_height_=0;
    unsigned processor_width_=0,processor_height_=0,upload_width_=0,upload_height_=0;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11VideoDevice> video_device_; ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_; ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> target_,upload_;
};

class Decoder {
public:
    using Consumer=std::function<void(IMFSample*,IMFMediaType*)>;
    Decoder(Renderer& renderer,const VideoConfig& config,bool software,Consumer consume)
        : consume_(std::move(consume)), config_(config) {
        require(CoCreateInstance(CLSID_CMSH264DecoderMFT,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&transform_)),"H.264 decoder");
        ComPtr<IMFAttributes> attrs; require(transform_->GetAttributes(&attrs),"decoder attributes");
        attrs->SetUINT32(MF_LOW_LATENCY,TRUE);
        UINT32 aware=0; attrs->GetUINT32(MF_SA_D3D11_AWARE,&aware);
        if (!software && aware) {
            UINT token=0; require(MFCreateDXGIDeviceManager(&token,&manager_),"DXGI manager");
            require(manager_->ResetDevice(renderer.device.Get(),token),"DXGI device binding");
            require(transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager_.Get())),"decoder D3D manager");
            uses_manager=true;
        }
        ComPtr<IMFMediaType> input; require(MFCreateMediaType(&input),"input media type");
        require(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"input major type");
        require(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264),"input subtype");
        require(MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,config.width,config.height),"input dimensions");
        require(MFSetAttributeRatio(input.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),"pixel aspect ratio");
        require(input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),"interlace mode");
        require(transform_->SetInputType(0,input.Get(),0),"decoder input type");
        select_output();
        require(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0),"begin streaming");
        require(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),"start stream");
    }
    ~Decoder() { if(transform_) { transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0); transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING,0); } }
    bool uses_manager=false;
    void submit(const Message& message) {
        std::vector<std::uint8_t> bytes;
        if (first_) bytes=config_.parameter_sets;
        bytes.insert(bytes.end(),message.payload.begin(),message.payload.end());
        ComPtr<IMFMediaBuffer> buffer; require(MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()),&buffer),"input buffer");
        BYTE* data=nullptr; require(buffer->Lock(&data,nullptr,nullptr),"input lock");
        std::memcpy(data,bytes.data(),bytes.size()); buffer->Unlock();
        require(buffer->SetCurrentLength(static_cast<DWORD>(bytes.size())),"input length");
        ComPtr<IMFSample> sample; require(MFCreateSample(&sample),"input sample");
        require(sample->AddBuffer(buffer.Get()),"attach input buffer");
        if (first_) first_pts_=message.header.timestamp_us;
        require(sample->SetSampleTime(static_cast<LONGLONG>(message.header.timestamp_us-first_pts_)*10),"sample timestamp");
        require(sample->SetUINT32(MFSampleExtension_CleanPoint,message.header.flags&1),"sample clean point");
        if (first_) sample->SetUINT32(MFSampleExtension_Discontinuity,TRUE);
        auto hr=transform_->ProcessInput(0,sample.Get(),0);
        if (hr==MF_E_NOTACCEPTING) { drain_output(); hr=transform_->ProcessInput(0,sample.Get(),0); }
        require(hr,"decode input"); first_=false; drain_output();
    }
    void finish() {
        require(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0),"end of stream");
        require(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0),"decoder drain");
        drain_output();
    }
private:
    void select_output() {
        for (DWORD i=0;;++i) {
            ComPtr<IMFMediaType> type;
            const auto hr=transform_->GetOutputAvailableType(0,i,&type);
            if (hr==MF_E_NO_MORE_TYPES) throw std::runtime_error("decoder has no NV12 output");
            require(hr,"enumerate decoder output"); GUID subtype{}; type->GetGUID(MF_MT_SUBTYPE,&subtype);
            if (subtype==MFVideoFormat_NV12) {
                UINT32 w=0,h=0; require(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h),"output dimensions");
                if (!w || !h || w>4096 || h>4096) throw std::runtime_error("decoder dimensions exceed budget");
                require(transform_->SetOutputType(0,type.Get(),0),"decoder NV12 output");
                output_type_=type; return;
            }
        }
    }
    void drain_output() {
        unsigned no_progress=0;
        for (;;) {
            MFT_OUTPUT_STREAM_INFO info{}; require(transform_->GetOutputStreamInfo(0,&info),"decoder output info");
            ComPtr<IMFSample> allocated;
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                if (info.cbSize>64*1024*1024) throw std::runtime_error("decoder output exceeds budget");
                require(MFCreateSample(&allocated),"output sample"); ComPtr<IMFMediaBuffer> buffer;
                require(MFCreateAlignedMemoryBuffer(info.cbSize,info.cbAlignment?info.cbAlignment-1:0,&buffer),"output buffer");
                require(allocated->AddBuffer(buffer.Get()),"attach output buffer");
            }
            MFT_OUTPUT_DATA_BUFFER output{}; output.pSample=allocated.Get(); DWORD status=0;
            const auto hr=transform_->ProcessOutput(0,1,&output,&status);
            ComPtr<IMFSample> decoded;
            if (output.pSample==allocated.Get()) decoded=allocated;
            else if (output.pSample) decoded.Attach(output.pSample);
            if (output.pEvents) output.pEvents->Release();
            if (hr==MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            if (hr==MF_E_TRANSFORM_STREAM_CHANGE) {
                if (++no_progress>8) throw std::runtime_error("repeated decoder format change");
                select_output(); continue;
            }
            require(hr,"decode output");
            if (decoded) { no_progress=0; consume_(decoded.Get(),output_type_.Get()); }
            else if (++no_progress>8) throw std::runtime_error("decoder output made no progress");
        }
    }
    ComPtr<IMFTransform> transform_; ComPtr<IMFDXGIDeviceManager> manager_; ComPtr<IMFMediaType> output_type_;
    Consumer consume_; VideoConfig config_; bool first_=true; std::uint64_t first_pts_=0;
};
}

PlaybackStats play_capture(const std::filesystem::path& path,const PlaybackOptions& options,
    std::stop_token stop,std::function<void(const PlaybackStats&)> on_complete) {
    Runtime runtime;
    std::ifstream file(path,std::ios::binary); if(!file) throw std::runtime_error("cannot open capture file");
    CaptureValidator validator;
    auto first=read_message(file); if(!first) throw ProtocolError("empty capture");
    validator.accept(*first); const auto& config=validator.config();
    Renderer renderer(options.window,config.width,config.height);
    PlaybackStats stats; stats.width=config.width; stats.height=config.height;
    ComPtr<IMFSample> last; ComPtr<IMFMediaType> last_type;
    Waiter wait; auto start=std::chrono::steady_clock::now(); bool clock_started=false;
    Decoder decoder(renderer,config,options.software,[&](IMFSample* sample,IMFMediaType* type) {
        if (stop.stop_requested()) return;
        if(options.paced) {
            LONGLONG timestamp=0; require(sample->GetSampleTime(&timestamp),"decoded timestamp");
            if(timestamp<0 || timestamp>3600ll*10000000) throw std::runtime_error("invalid decoded timestamp");
            if(!clock_started) { start=std::chrono::steady_clock::now()-std::chrono::microseconds(timestamp/10); clock_started=true; }
            wait.until(stop,start+std::chrono::microseconds(timestamp/10));
        }
        if(stop.stop_requested()) return;
        const bool gpu=renderer.render(sample,type); ++stats.decoded_frames;
        if(gpu) ++stats.gpu_frames; else ++stats.cpu_frames;
        last=sample; last_type=type;
    });
    stats.d3d_manager=decoder.uses_manager;
    while(!stop.stop_requested()) {
        auto message=read_message(file); if(!message) break;
        validator.accept(*message);
        decoder.submit(*message); ++stats.input_frames;
    }
    if(stop.stop_requested()) return stats;
    validator.finish(); decoder.finish();
    if(!stats.decoded_frames) throw std::runtime_error("decoder produced no frames");
    if(stats.input_frames!=stats.decoded_frames) throw std::runtime_error("decoded frame count does not match input");
    if(!options.snapshot.empty()) renderer.snapshot(options.snapshot);
    if(on_complete) on_complete(stats);
    while(options.hold_last_frame && !stop.stop_requested()) {
        wait.until(stop,std::chrono::steady_clock::now()+std::chrono::milliseconds(100));
        if(!stop.stop_requested() && last && renderer.resized()) renderer.render(last.Get(),last_type.Get());
    }
    return stats;
}
}
