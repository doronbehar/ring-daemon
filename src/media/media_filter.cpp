/*
 *  Copyright (C) 2018 Savoir-faire Linux Inc.
 *
 *  Author: Philippe Gorley <philippe.gorley@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#include "libav_deps.h" // MUST BE INCLUDED FIRST
#include "logger.h"
#include "media_filter.h"

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>

namespace ring {

MediaFilter::MediaFilter()
{}

MediaFilter::~MediaFilter()
{
    clean();
}

std::string
MediaFilter::getFilterDesc() const
{
    return desc_;
}

int
MediaFilter::initialize(const std::string& filterDesc, MediaStream msp)
{
    std::vector<MediaStream> msps;
    msps.push_back(msp);
    desc_ = filterDesc;
    return initialize(desc_, msps);
}

int
MediaFilter::initialize(const std::string& filterDesc, std::vector<MediaStream> msps)
{
    int ret = 0;
    desc_ = filterDesc;
    graph_ = avfilter_graph_alloc();

    if (!graph_)
        return fail("Failed to allocate filter graph", AVERROR(ENOMEM));

    graph_->nb_threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()/2));

    AVFilterInOut* in;
    AVFilterInOut* out;
    if ((ret = avfilter_graph_parse2(graph_, desc_.c_str(), &in, &out)) < 0)
        return fail("Failed to parse filter graph", ret);

    using AVFilterInOutPtr = std::unique_ptr<AVFilterInOut, std::function<void(AVFilterInOut*)>>;
    AVFilterInOutPtr outputs(out, [](AVFilterInOut* f){ avfilter_inout_free(&f); });
    AVFilterInOutPtr inputs(in, [](AVFilterInOut* f){ avfilter_inout_free(&f); });

    if (outputs && outputs->next)
        return fail("Filters with multiple outputs are not supported", AVERROR(ENOTSUP));

    if ((ret = initOutputFilter(outputs.get())) < 0)
        return fail("Failed to create output for filter graph", ret);

    // make sure inputs linked list is the same size as msps
    size_t count = 0;
    AVFilterInOut* dummyInput = inputs.get();
    while (dummyInput && ++count) // increment count before evaluating its value
        dummyInput = dummyInput->next;
    if (count != msps.size())
        return fail("Size mismatch between number of inputs in filter graph and input parameter array",
                    AVERROR(EINVAL));

    if (count > 1) {
        /* Complex filter */
        for (AVFilterInOut* current = inputs.get(); current; current = current->next) {
            if (!current->name)
                return fail("Complex filters' inputs require names", AVERROR(EINVAL));
            std::string name = current->name;
            const auto& it = std::find_if(msps.begin(), msps.end(), [name](const MediaStream& msp)
                    { return msp.name == name; });
            if (it != msps.end()) {
                if ((ret = initInputFilter(current, *it, false)) < 0) {
                    std::string msg = "Failed to find matching parameters for: " + name;
                    return fail(msg, ret);
                }
            }
        }
    } else {
        /* Simple filter */
        if ((ret = initInputFilter(inputs.get(), msps[0], true)) < 0) {
            return fail("Failed to create input for filter graph", ret);
        }
    }

    if ((ret = avfilter_graph_config(graph_, nullptr)) < 0)
        return fail("Failed to configure filter graph", ret);

    RING_DBG() << "Filter graph initialized with: " << desc_;
    initialized_ = true;
    return 0;
}

MediaStream
MediaFilter::getOutputParams()
{
    MediaStream output;
    if (!output_ || !initialized_) {
        fail("Filter not initialized", -1);
        return output;
    }

    switch (av_buffersink_get_type(output_)) {
    case AVMEDIA_TYPE_VIDEO:
        output.name = "videoOutput";
        output.format = av_buffersink_get_format(output_);
        output.isVideo = true;
        output.timeBase = av_buffersink_get_time_base(output_);
        output.width = av_buffersink_get_w(output_);
        output.height = av_buffersink_get_h(output_);
        output.aspectRatio = av_buffersink_get_sample_aspect_ratio(output_);
        output.frameRate = av_buffersink_get_frame_rate(output_);
        break;
    case AVMEDIA_TYPE_AUDIO:
        output.name = "audioOutput";
        output.format = av_buffersink_get_format(output_);
        output.isVideo = false;
        output.timeBase = av_buffersink_get_time_base(output_);
        output.sampleRate = av_buffersink_get_sample_rate(output_);
        output.nbChannels = av_buffersink_get_channels(output_);
        break;
    default:
        output.format = -1;
        break;
    }
    return output;
}

int
MediaFilter::feedInput(AVFrame* frame)
{
    return feedInput(frame, "default");
}

int
MediaFilter::feedInput(AVFrame* frame, std::string inputName)
{
    int ret = 0;
    if (!initialized_)
        return fail("Filter not initialized", -1);

    for (size_t i = 0; i < inputs_.size(); ++i) {
        auto filterCtx = inputs_[i];
        if (inputNames_[i] != inputName)
            continue;

        int flags = AV_BUFFERSRC_FLAG_PUSH | AV_BUFFERSRC_FLAG_KEEP_REF;
        if ((ret = av_buffersrc_add_frame_flags(filterCtx, frame, flags)) < 0)
            return fail("Could not pass frame to filters", ret);
        else
            return 0;
    }

    std::stringstream ss;
    ss << "Specified filter (" << inputName << ") not found";
    return fail(ss.str(), AVERROR(EINVAL));
}

AVFrame*
MediaFilter::readOutput()
{
    if (!initialized_) {
        fail("Not properly initialized", -1);
        return nullptr;
    }

    int ret = 0;
    AVFrame* frame = av_frame_alloc();
    ret = av_buffersink_get_frame_flags(output_, frame, 0);
    if (ret >= 0) {
        return frame;
    } else if (ret == AVERROR(EAGAIN)) {
        // return nullptr
    } else if (ret == AVERROR_EOF) {
        RING_WARN() << "Filters have reached EOF, no more frames will be output";
    } else {
        fail("Error occurred while pulling from filter graph", ret);
    }
    av_frame_free(&frame);
    return nullptr;
}

int
MediaFilter::initOutputFilter(AVFilterInOut* out)
{
    int ret = 0;
    const AVFilter* buffersink;
    AVFilterContext* buffersinkCtx = nullptr;
    AVMediaType mediaType = avfilter_pad_get_type(out->filter_ctx->input_pads, out->pad_idx);

    if (mediaType == AVMEDIA_TYPE_VIDEO)
        buffersink = avfilter_get_by_name("buffersink");
    else
        buffersink = avfilter_get_by_name("abuffersink");

    if ((ret = avfilter_graph_create_filter(&buffersinkCtx, buffersink, "out",
                                            nullptr, nullptr, graph_)) < 0) {
        avfilter_free(buffersinkCtx);
        return fail("Failed to create buffer sink", ret);
    }

    if ((ret = avfilter_link(out->filter_ctx, out->pad_idx, buffersinkCtx, 0)) < 0) {
        avfilter_free(buffersinkCtx);
        return fail("Could not link buffer sink to graph", ret);
    }

    output_ = buffersinkCtx;
    return ret;
}

int
MediaFilter::initInputFilter(AVFilterInOut* in, MediaStream msp, bool simple)
{
    int ret = 0;
    AVBufferSrcParameters* params = av_buffersrc_parameters_alloc();
    if (!params)
        return -1;

    const AVFilter* buffersrc;
    AVMediaType mediaType = avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx);
    params->format = msp.format;
    params->time_base = msp.timeBase;
    if (mediaType == AVMEDIA_TYPE_VIDEO) {
        params->width = msp.width;
        params->height = msp.height;
        params->sample_aspect_ratio = msp.aspectRatio;
        params->frame_rate = msp.frameRate;
        buffersrc = avfilter_get_by_name("buffer");
    } else {
        params->sample_rate = msp.sampleRate;
        params->channel_layout = av_get_default_channel_layout(msp.nbChannels);
        buffersrc = avfilter_get_by_name("abuffer");
    }

    AVFilterContext* buffersrcCtx = nullptr;
    if (buffersrc) {
        char name[128];
        if (simple)
            snprintf(name, sizeof(name), "buffersrc");
        else
            snprintf(name, sizeof(name), "buffersrc_%s_%d", in->name, in->pad_idx);
        buffersrcCtx = avfilter_graph_alloc_filter(graph_, buffersrc, name);
    }
    if (!buffersrcCtx) {
        av_free(params);
        return fail("Failed to allocate filter graph input", AVERROR(ENOMEM));
    }
    ret = av_buffersrc_parameters_set(buffersrcCtx, params);
    av_free(params);
    if (ret < 0)
        return fail("Failed to set filter graph input parameters", ret);

    if ((ret = avfilter_init_str(buffersrcCtx, nullptr)) < 0)
        return fail("Failed to initialize buffer source", ret);

    if ((ret = avfilter_link(buffersrcCtx, 0, in->filter_ctx, in->pad_idx)) < 0)
        return fail("Failed to link buffer source to graph", ret);

    inputs_.push_back(buffersrcCtx);
    if (simple)
        inputNames_.push_back("default");
    else
        inputNames_.push_back(in->name);
    return ret;
}

int
MediaFilter::fail(std::string msg, int err)
{
    if (!msg.empty())
        RING_ERR() << msg << ": " << libav_utils::getError(err);
    return err;
}

void
MediaFilter::clean()
{
    avfilter_graph_free(&graph_);
    initialized_ = false;
}

} // namespace ring