#pragma once

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class VideoPlayer {
public:
    struct Frame {
        int width = 0;
        int height = 0;
        uint64_t serial = 0;
        std::vector<uint8_t> rgba;
    };

    VideoPlayer(std::string source, bool autoplay, bool muted, bool loop)
        : source_(std::move(source)), muted_(muted), loop_(loop),
          start_requested_(autoplay),
          worker_(&VideoPlayer::decode, this) {}

    ~VideoPlayer() {
        stopping_.store(true);
        playing_.store(true);
        if (worker_.joinable()) worker_.join();
    }

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool is_ready() const { return ready_.load(); }
    bool is_playing() const { return playing_.load(); }
    bool is_finished() const { return finished_.load(); }
    int64_t duration_ms() const { return duration_ms_.load(); }
    int quality_height() const { return quality_height_.load(); }
    double playback_rate() const { return playback_rate_.load(); }
    bool is_muted() const { return muted_.load(); }
    int volume() const { return volume_.load(); }

    int64_t position_ms() const {
        if (!playing_.load()) return base_position_ms_.load();
        const Uint64 started = wall_clock_start_.load();
        int64_t position = base_position_ms_.load() +
            static_cast<int64_t>((SDL_GetTicks64() - started) * playback_rate_.load());
        const int64_t duration = duration_ms_.load();
        return duration > 0 ? std::min(duration, position) : position;
    }

    std::shared_ptr<const Frame> latest_frame() {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        return frame_;
    }

    uint64_t frame_serial() {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        return frame_ ? frame_->serial : 0;
    }

    std::string error() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return error_;
    }

    void toggle_playback() {
        if (!ready_.load()) {
            start_requested_.store(!start_requested_.load());
            return;
        }
        if (playing_.load()) pause();
        else play();
    }

    void play() {
        if (!ready_.load()) {
            start_requested_.store(true);
            return;
        }
        if (stopping_.load()) return;
        if (finished_.load()) {
            seek_request_ms_.store(0);
            seek_requested_.store(true);
            finished_.store(false);
        }
        wall_clock_start_.store(SDL_GetTicks64());
        playing_.store(true);
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (audio_device_) SDL_PauseAudioDevice(audio_device_, 0);
    }

    void pause() {
        if (!playing_.load()) return;
        const int64_t position = position_ms();
        playing_.store(false);
        base_position_ms_.store(position);
        wall_clock_start_.store(SDL_GetTicks64());
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (audio_device_) {
            SDL_PauseAudioDevice(audio_device_, 1);
            SDL_ClearQueuedAudio(audio_device_);
        }
    }

    void seek(int64_t position_ms) {
        if (!ready_.load() || duration_ms_.load() <= 0) return;
        const int64_t position = std::max<int64_t>(0, std::min(position_ms, duration_ms_.load()));
        base_position_ms_.store(position);
        wall_clock_start_.store(SDL_GetTicks64());
        seek_request_ms_.store(position);
        seek_requested_.store(true);
    }

    void set_playback_rate(double rate) {
        const int64_t position = position_ms();
        base_position_ms_.store(position);
        wall_clock_start_.store(SDL_GetTicks64());
        playback_rate_.store(std::max(0.25, std::min(3.0, rate)));
        speed_change_requested_.store(true);
    }

    void set_quality_height(int height) {
        quality_height_.store(height);
    }

    void set_output_size(int width, int height) {
        output_width_.store(std::max(0, width));
        output_height_.store(std::max(0, height));
    }

    void toggle_muted() {
        muted_.store(!muted_.load());
    }

    void set_volume(int volume) {
        volume_.store(std::max(0, std::min(100, volume)));
        if (volume_.load() > 0) muted_.store(false);
    }

    void change_source(std::string source) {
        if (source.empty()) return;
        const int64_t position = position_ms();
        {
            std::lock_guard<std::mutex> lock(source_mutex_);
            requested_source_ = std::move(source);
            source_should_play_.store(playing_.load() || start_requested_.load());
            requested_source_position_ms_.store(position);
        }
        source_change_requested_.store(true);
        playing_.store(false);
    }

private:
    static constexpr int requested_audio_frequency = 48000;

    std::string source_;
    std::atomic<bool> muted_{false};
    bool loop_ = false;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> start_requested_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> seek_requested_{false};
    std::atomic<bool> speed_change_requested_{false};
    std::atomic<bool> source_change_requested_{false};
    std::atomic<bool> source_should_play_{false};
    std::atomic<int> quality_height_{1080};
    std::atomic<int> output_width_{0};
    std::atomic<int> output_height_{0};
    std::atomic<int> volume_{80};
    std::atomic<double> playback_rate_{1.0};
    std::atomic<int64_t> seek_request_ms_{0};
    std::atomic<int64_t> requested_source_position_ms_{0};
    std::atomic<int64_t> duration_ms_{0};
    std::atomic<int64_t> base_position_ms_{0};
    std::atomic<Uint64> wall_clock_start_{0};
    std::thread worker_;
    mutable std::mutex error_mutex_;
    std::string error_;
    std::mutex frame_mutex_;
    mutable std::mutex audio_mutex_;
    std::mutex source_mutex_;
    std::string requested_source_;
    std::shared_ptr<const Frame> frame_;
    uint64_t next_frame_serial_ = 0;

    AVFormatContext* format_ = nullptr;
    AVCodecContext* video_codec_ = nullptr;
    AVCodecContext* audio_codec_ = nullptr;
    SwsContext* scaler_ = nullptr;
    SwrContext* resampler_ = nullptr;
    AVFrame* decoded_frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    int video_stream_index_ = -1;
    int audio_stream_index_ = -1;
    AVRational video_time_base_{1, 1};
    int64_t video_start_pts_ = 0;
    int64_t video_segment_start_pts_ = 0;
    SDL_AudioDeviceID audio_device_ = 0;
    int audio_frequency_ = requested_audio_frequency;
    int audio_channels_ = 2;
    int resampler_output_frequency_ = requested_audio_frequency;

    static int interrupt_callback(void* opaque) {
        const auto* player = static_cast<const VideoPlayer*>(opaque);
        return player->stopping_.load() || player->source_change_requested_.load() ? 1 : 0;
    }

    void set_error(const std::string& message) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_ = message;
        std::cerr << "Video playback error: " << message << "\n";
    }

    bool open_codec(int stream_index, AVCodecContext*& context) {
        AVStream* stream = format_->streams[stream_index];
        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) return false;
        context = avcodec_alloc_context3(decoder);
        if (!context) return false;
        if (avcodec_parameters_to_context(context, stream->codecpar) < 0 ||
            avcodec_open2(context, decoder, nullptr) < 0) {
            avcodec_free_context(&context);
            return false;
        }
        return true;
    }

    bool open_media() {
        format_ = avformat_alloc_context();
        if (!format_) {
            set_error("Could not allocate a media context.");
            return false;
        }
        format_->interrupt_callback.callback = interrupt_callback;
        format_->interrupt_callback.opaque = this;

        AVDictionary* options = nullptr;
        av_dict_set(&options, "rw_timeout", "60000000", 0);
        const int open_result = avformat_open_input(&format_, source_.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (open_result < 0) {
            set_error("Could not open " + source_ + ": " + av_error(open_result));
            return false;
        }

        const int info_result = avformat_find_stream_info(format_, nullptr);
        if (info_result < 0) {
            set_error("Could not read media information: " + av_error(info_result));
            return false;
        }

        for (unsigned int index = 0; index < format_->nb_streams; ++index) {
            if (format_->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index_ < 0) {
                video_stream_index_ = static_cast<int>(index);
            } else if (format_->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index_ < 0) {
                audio_stream_index_ = static_cast<int>(index);
            }
        }
        if (video_stream_index_ < 0 || !open_codec(video_stream_index_, video_codec_)) {
            set_error("The media does not contain a supported video stream.");
            return false;
        }
        video_time_base_ = format_->streams[video_stream_index_]->time_base;
        video_start_pts_ = format_->streams[video_stream_index_]->start_time == AV_NOPTS_VALUE
            ? 0 : format_->streams[video_stream_index_]->start_time;
        video_segment_start_pts_ = video_start_pts_;

        int64_t duration = format_->duration;
        if (duration <= 0 && format_->streams[video_stream_index_]->duration > 0) {
            duration = av_rescale_q(format_->streams[video_stream_index_]->duration,
                                    video_time_base_, AV_TIME_BASE_Q);
        }
        if (duration > 0) duration_ms_.store(duration / (AV_TIME_BASE / 1000));

        decoded_frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        if (!decoded_frame_ || !packet_) {
            set_error("Could not allocate video decoding buffers.");
            return false;
        }

        if (audio_stream_index_ >= 0) {
            if (!open_codec(audio_stream_index_, audio_codec_)) {
                std::cerr << "Video playback warning: the audio track is not supported.\n";
            } else {
                SDL_AudioSpec desired{};
                desired.freq = requested_audio_frequency;
                desired.format = AUDIO_S16SYS;
                desired.channels = 2;
                desired.samples = 1024;
                audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
                if (audio_device_) {
                    audio_frequency_ = desired.freq;
                    audio_channels_ = desired.channels;
                    AVChannelLayout output_layout;
                    av_channel_layout_default(&output_layout, audio_channels_);
                    resampler_output_frequency_ = static_cast<int>(audio_frequency_ * playback_rate_.load());
                    if (swr_alloc_set_opts2(&resampler_, &output_layout, AV_SAMPLE_FMT_S16, resampler_output_frequency_,
                                            &audio_codec_->ch_layout, audio_codec_->sample_fmt,
                                            audio_codec_->sample_rate, 0, nullptr) < 0 ||
                        !resampler_ || swr_init(resampler_) < 0) {
                        std::cerr << "Video playback warning: audio conversion is unavailable.\n";
                        swr_free(&resampler_);
                        SDL_CloseAudioDevice(audio_device_);
                        audio_device_ = 0;
                    }
                    av_channel_layout_uninit(&output_layout);
                } else {
                    std::cerr << "Video playback warning: audio is unavailable: " << SDL_GetError() << "\n";
                }
            }
        }

        ready_.store(true);
        if (start_requested_.exchange(false)) play();
        return true;
    }

    static std::string av_error(int error) {
        char buffer[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(error, buffer, sizeof(buffer));
        return buffer;
    }

    bool handle_seek() {
        if (!seek_requested_.exchange(false)) return false;
        const int64_t target_ms = seek_request_ms_.load();
        const int64_t timestamp = av_rescale_q(target_ms, AVRational{1, 1000}, video_time_base_);
        const int result = av_seek_frame(format_, video_stream_index_, video_start_pts_ + timestamp, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            set_error("Could not seek in video: " + av_error(result));
            return true;
        }
        avcodec_flush_buffers(video_codec_);
        if (audio_codec_) avcodec_flush_buffers(audio_codec_);
        video_segment_start_pts_ = video_start_pts_ + timestamp;
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (audio_device_) {
            SDL_ClearQueuedAudio(audio_device_);
            SDL_PauseAudioDevice(audio_device_, playing_.load() ? 0 : 1);
        }
        base_position_ms_.store(target_ms);
        wall_clock_start_.store(SDL_GetTicks64());
        finished_.store(false);
        return true;
    }

    void publish_video_frame(AVFrame* source) {
        const int source_width = source->width;
        const int source_height = source->height;
        const int requested_height = quality_height_.load();
        double scale = requested_height > 0 && source_height > requested_height
            ? static_cast<double>(requested_height) / source_height : 1.0;
        const int output_width = output_width_.load();
        const int output_height = output_height_.load();
        if (output_width > 0 && source_width > output_width) {
            scale = std::min(scale, static_cast<double>(output_width) / source_width);
        }
        if (output_height > 0 && source_height > output_height) {
            scale = std::min(scale, static_cast<double>(output_height) / source_height);
        }
        const int width = std::max(1, static_cast<int>(source_width * scale));
        const int height = std::max(1, static_cast<int>(source_height * scale));
        constexpr size_t maximum_frame_bytes = 256ull * 1024ull * 1024ull;
        if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4 ||
            static_cast<size_t>(height) > maximum_frame_bytes / (static_cast<size_t>(width) * 4)) {
            set_error("The decoded video frame is too large to display.");
            return;
        }
        scaler_ = sws_getCachedContext(scaler_, source_width, source_height,
                                       static_cast<AVPixelFormat>(source->format),
                                       width, height, AV_PIX_FMT_RGBA,
                                       SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler_) {
            set_error("Could not initialize video frame conversion.");
            return;
        }

        Frame updated;
        updated.width = width;
        updated.height = height;
        try {
            updated.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        } catch (const std::bad_alloc&) {
            set_error("There is not enough memory to display the video frame.");
            return;
        }
        uint8_t* output[4] = { updated.rgba.data(), nullptr, nullptr, nullptr };
        int output_linesize[4] = { width * 4, 0, 0, 0 };
        sws_scale(scaler_, source->data, source->linesize, 0, source_height, output, output_linesize);
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            updated.serial = ++next_frame_serial_;
            try {
                frame_ = std::make_shared<Frame>(std::move(updated));
            } catch (const std::bad_alloc&) {
                set_error("There is not enough memory to publish the video frame.");
            }
        }
    }

    bool wait_for_video_time(int64_t pts) {
        if (pts == AV_NOPTS_VALUE) return true;
        const AVStream* stream = format_->streams[video_stream_index_];
        const int64_t elapsed = av_rescale_q(pts - video_segment_start_pts_, stream->time_base, AVRational{1, 1000});
        const int64_t target_ms = base_position_ms_.load() + std::max<int64_t>(0, elapsed);
        while (!stopping_.load() && playing_.load() && !seek_requested_.load()) {
            const int64_t delay = target_ms - position_ms();
            if (delay <= 0) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(delay, 10)));
        }
        return false;
    }

    void handle_decoded_frame(AVCodecContext* decoder, int stream_index) {
        while (!stopping_.load() && avcodec_receive_frame(decoder, decoded_frame_) >= 0) {
            if (stream_index == video_stream_index_) {
                if (wait_for_video_time(decoded_frame_->best_effort_timestamp)) {
                    publish_video_frame(decoded_frame_);
                }
            } else if (stream_index == audio_stream_index_ && resampler_ && audio_device_ && playing_.load()) {
                const int output_capacity = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(resampler_, audio_codec_->sample_rate) + decoded_frame_->nb_samples,
                    resampler_output_frequency_, audio_codec_->sample_rate, AV_ROUND_UP));
                std::vector<uint8_t> audio(static_cast<size_t>(output_capacity) * audio_channels_ * 2);
                uint8_t* output[] = { audio.data() };
                const uint8_t** input = const_cast<const uint8_t**>(decoded_frame_->extended_data);
                const int converted_samples = swr_convert(resampler_, output, output_capacity,
                                                           input,
                                                           decoded_frame_->nb_samples);
                if (converted_samples > 0) {
                    const Uint32 byte_count = static_cast<Uint32>(converted_samples * audio_channels_ * 2);
                    auto* samples = reinterpret_cast<int16_t*>(audio.data());
                    const int sample_count = converted_samples * audio_channels_;
                    const float gain = muted_.load() ? 0.0f : volume_.load() / 100.0f;
                    if (gain != 1.0f) {
                        for (int index = 0; index < sample_count; ++index) {
                            samples[index] = static_cast<int16_t>(samples[index] * gain);
                        }
                    }
                    const Uint32 maximum_queued_bytes = static_cast<Uint32>(
                        audio_frequency_ * audio_channels_ * sizeof(int16_t) / 5);
                    while (!stopping_.load() && playing_.load() &&
                           queued_audio_bytes() > maximum_queued_bytes) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    if (playing_.load()) {
                        std::lock_guard<std::mutex> lock(audio_mutex_);
                        if (audio_device_ && playing_.load()) {
                            if (SDL_QueueAudio(audio_device_, audio.data(), byte_count) < 0) {
                                set_error("Could not queue video audio: " + std::string(SDL_GetError()));
                            }
                        }
                    }
                }
            }
            av_frame_unref(decoded_frame_);
            if (!playing_.load() || seek_requested_.load()) break;
        }
    }

    void drain_audio() {
        while (!stopping_.load() && playing_.load() && queued_audio_bytes() > 0) {
            if (seek_requested_.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void decode_packet(AVCodecContext* decoder, int stream_index) {
        int result = avcodec_send_packet(decoder, packet_);
        if (result == AVERROR(EAGAIN)) {
            handle_decoded_frame(decoder, stream_index);
            if (!playing_.load() || seek_requested_.load()) return;
            result = avcodec_send_packet(decoder, packet_);
        }
        if (result < 0) {
            const std::string message = "Could not decode a media packet: " + av_error(result);
            if (stream_index == video_stream_index_) set_error(message);
            else std::cerr << "Video playback warning: " << message << "\n";
            return;
        }
        handle_decoded_frame(decoder, stream_index);
    }

    void apply_speed_change() {
        if (!speed_change_requested_.exchange(false) || !resampler_ || !audio_codec_) return;
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (!audio_device_) return;
        SDL_ClearQueuedAudio(audio_device_);
        swr_close(resampler_);
        resampler_output_frequency_ = static_cast<int>(audio_frequency_ * playback_rate_.load());
        AVChannelLayout output_layout;
        av_channel_layout_default(&output_layout, audio_channels_);
        const int allocation_result = swr_alloc_set_opts2(&resampler_, &output_layout, AV_SAMPLE_FMT_S16,
                                resampler_output_frequency_, &audio_codec_->ch_layout,
                                audio_codec_->sample_fmt, audio_codec_->sample_rate, 0, nullptr) < 0 ||
            swr_init(resampler_) < 0;
        av_channel_layout_uninit(&output_layout);
        if (allocation_result) {
            std::cerr << "Video playback warning: could not apply playback speed to audio.\n";
            swr_free(&resampler_);
            return;
        }
        wall_clock_start_.store(SDL_GetTicks64());
    }

    bool apply_source_change() {
        if (!source_change_requested_.exchange(false)) return false;
        std::string next_source;
        {
            std::lock_guard<std::mutex> lock(source_mutex_);
            next_source = std::move(requested_source_);
        }
        const bool should_play = source_should_play_.exchange(false);
        cleanup();
        source_ = std::move(next_source);
        ready_.store(false);
        finished_.store(false);
        duration_ms_.store(0);
        base_position_ms_.store(0);
        seek_requested_.store(false);
        speed_change_requested_.store(false);
        video_stream_index_ = -1;
        audio_stream_index_ = -1;
        video_start_pts_ = 0;
        video_segment_start_pts_ = 0;
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            frame_.reset();
        }
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            error_.clear();
        }
        if (open_media()) {
            const int64_t target_position = std::min(requested_source_position_ms_.load(), duration_ms_.load());
            if (target_position > 0) {
                seek_request_ms_.store(target_position);
                seek_requested_.store(true);
                handle_seek();
            }
            if (should_play) play();
        } else {
            cleanup();
        }
        return true;
    }

    Uint32 queued_audio_bytes() const {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        return audio_device_ ? SDL_GetQueuedAudioSize(audio_device_) : 0;
    }

    void decode() {
        static std::once_flag network_initialization;
        std::call_once(network_initialization, avformat_network_init);
        if (!open_media()) {
            cleanup();
            return;
        }

        while (!stopping_.load()) {
            if (source_change_requested_.load()) {
                apply_source_change();
                continue;
            }
            if (speed_change_requested_.load()) apply_speed_change();
            if (seek_requested_.load()) {
                handle_seek();
                continue;
            }
            if (!playing_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            handle_decoded_frame(video_codec_, video_stream_index_);
            if (audio_codec_) handle_decoded_frame(audio_codec_, audio_stream_index_);
            if (!playing_.load() || seek_requested_.load()) continue;

            const int result = av_read_frame(format_, packet_);
            if (result == AVERROR_EOF) {
                if (avcodec_send_packet(video_codec_, nullptr) >= 0) {
                    handle_decoded_frame(video_codec_, video_stream_index_);
                }
                if (audio_codec_ && avcodec_send_packet(audio_codec_, nullptr) >= 0) {
                    handle_decoded_frame(audio_codec_, audio_stream_index_);
                }
                drain_audio();
                if (stopping_.load()) break;
                if (seek_requested_.load()) continue;
                if (loop_) {
                    seek_request_ms_.store(0);
                    seek_requested_.store(true);
                } else {
                    if (duration_ms_.load() > 0) base_position_ms_.store(duration_ms_.load());
                    playing_.store(false);
                    finished_.store(true);
                    if (audio_device_) SDL_PauseAudioDevice(audio_device_, 1);
                }
                continue;
            }
            if (result < 0) {
                if (result != AVERROR(EAGAIN)) set_error("Media read failed: " + av_error(result));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (packet_->stream_index == video_stream_index_) {
                decode_packet(video_codec_, video_stream_index_);
            } else if (packet_->stream_index == audio_stream_index_ && audio_codec_) {
                decode_packet(audio_codec_, audio_stream_index_);
            }
            av_packet_unref(packet_);
        }
        cleanup();
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (audio_device_) {
            SDL_PauseAudioDevice(audio_device_, 1);
            SDL_ClearQueuedAudio(audio_device_);
            SDL_CloseAudioDevice(audio_device_);
            audio_device_ = 0;
        }
        sws_freeContext(scaler_);
        scaler_ = nullptr;
        swr_free(&resampler_);
        av_frame_free(&decoded_frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&video_codec_);
        avcodec_free_context(&audio_codec_);
        avformat_close_input(&format_);
    }
};
