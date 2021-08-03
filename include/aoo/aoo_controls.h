#pragma once

#include "aoo_defines.h"

#define AOO_ARG(x) ((void *)&x), sizeof(x)

// AoO controls that can be passed to *_ctl function.
// *_ctl function arguments:
// AooCtl 'ctl': control code
// intptr_t 'index': special index; can also be AooEndpoint *
// void * 'data': argument data
// size_t 'size': argument size

enum AooControls
{
    // Set/get source/sink ID (arg: AooId)
    kAooCtlSetId = 0,
    kAooCtlGetId,
    // add/remove sink (index: sink endpoint)
    kAooCtlAddSink,
    kAooCtlRemoveSink,
    // invite/uninvite source (index: source endpoint)
    kAooCtlInviteSource,
    kAooCtlUninviteSource,
    // Set the source stream format (arg: AooFormat)
    // ---
    // Set the format by passing the format header.
    // The format struct is validated and updated on success!
    //
    // This will change the streaming format and consequently start a
    // new stream. The sink(s) will receive a kAooEventFormatChange event
    kAooCtlSetFormat,
    // Get the source stream format (arg: AooFormat)
    // ---
    // Get the current format by passing an instance of 'AooFormatStorage'
    // or a similar struct that is large enough to hold any format.
    // The 'size' member in the format header should contain the storage size;
    // on success it is updated to the actual format size
    kAooCtlGetFormat,
    // Request stream format (index: endpoint, arg: format struct)
    // ---
    // A sink can request the streaming format for a specific source,
    // which can choose to accept or decline the request.
    // The source will receive one or more kAooEventFormatRequest events.
    // If the request has timed out, the sink receives a kAooEventFormatTimeout event.
    kAooCtlRequestFormat,
    // Perform a codec control (index: codec control, arg: argument)
    // ---
    // The available codec controls should be listed in the AOO codecheader file.
    kAooCtlCodecControl,
    // Reset the source/sink (none)
    kAooCtlReset,
    // Start a new stream ([optional] arg: AooCustomData)
    // ---
    // Can be called from any thread. Realtime safe!
    // You can pass a AooCustomData structure which will be sent as additional
    // stream metadata. For example, it could contain information about the
    // channel layout, the musical content, etc.
    kAooCtlStartStream,
    // Stop the stream (none)
    kAooCtlStopStream,
    // Set/get buffer size in seconds (arg: AooSeconds)
    // ---
    // This is the size of the ring buffer
    // between the audio and network thread.
    // For the source, this can be rather small,
    // as you only have to compensate the latency
    // for thread signalling.
    // For the sink, a larger buffer size helps
    // to deal with network jitter, packet reordering
    // and packet loss. For local networks, small
    // buffersizes between 10-50ms should work,
    // but for unreliable/unpredictable networks
    // you might need to increase it significantly.
    kAooCtlSetBufferSize,
    kAooCtlGetBufferSize,
    // Enable/disable dynamic resampling (arg: AooBool)
    // ---
    // Dynamic resampling attempts to mitigate CPU clock drift
    // between two different machines.
    // A DLL filter estimates the effective sample rate on both sides,
    // and the audio data is resampled accordingly. The behavior
    // can be fine-tuned with the AOO_CTL_DLL_BANDWIDTH option.
    // See the paper "Using a DLL to filter time" by Fons Adriaensen.
    kAooCtlSetDynamicResampling,
    kAooCtlGetDynamicResampling,
    // Real samplerate (arg: AooSampleRate)
    // ---
    // Get effective samplerate as estimated by DLL.
    kAooCtlGetRealSampleRate,
    // Set/get DLL filter bandwidth (arg: AooSampleRate)
    // ---
    // Used for dynamic resampling, see kAooCtlSetDynamicResampling.
    kAooCtlSetDllBandwidth,
    kAooCtlGetDllBandwidth,
    // Enable/disable timer check (arg: AooBool)
    // ---
    // Enable to catch timing problems, e.g. when the host accidentally
    // blocks the audio callback, which would confuse the time DLL filter.
    // Also, timing gaps are handled by sending empty blocks at the source
    // resp. dropping blocks at the sink.
    // NOTE: only takes effect on source/sink setup!
    kAooCtlSetTimerCheck,
    kAooCtlGetTimerCheck,
    // Set/get sink channel onset (index: sink endpoint, arg: int32_t)
    // ---
    // The channel onset of the sink where a given source
    // should be received. For example, if the channel onset
    // is 5, a 2-channel source will be summed into sink
    // channels 5 and 6. The default is 0 (= the first channel).
    kAooCtlSetChannelOnset,
    kAooCtlGetChannelOnset,
    // Set/get max. UDP packet size in bytes (arg: int32_t)
    // ---
    // The default value of 512 should be fine for most
    // networks (even the internet), but you might increase
    // this value for local networks because larger packet sizes
    // have less overhead. If a audio block exceeds the max.
    // UDP packet size, it will be automatically broken up
    // into several "frames" and then reassembled in the sink.
    kAooCtlSetPacketSize,
    kAooCtlGetPacketSize,
    // Set/get ping interval in seconds (arg: AooSeconds)
    // ---
    // The sink sends a periodic ping message to each
    // source to signify that it is actually receiving data.
    // For example, a application might choose to remove
    // a sink after the source hasn't received a ping
    // for a certain amount of time.
    kAooCtlSetPingInterval,
    kAooCtlGetPingInterval,
    // Enable/disable data resending (arg: AooBool)
    kAooCtlSetResendData,
    kAooCtlGetResendData,
    // Set/get resend buffer size in seconds (arg: AooSeconds).
    // ---
    // The source keeps the last N seconds of audio in a buffer,
    // so it can resend parts of it, if requested, e.g. to
    // handle packet loss.
    kAooCtlSetResendBufferSize,
    kAooCtlGetResendBufferSize,
    // Set/get resend interval in seconds (arg: AooSeconds)
    // ---
    // This is the interval between individual resend
    // attempts for a specific frame.
    // Since there is always a certain roundtrip delay
    // between source and sink, it makes sense to wait
    // between resend attempts to not spam the network
    // with redundant /resend messages.
    kAooCtlSetResendInterval,
    kAooCtlGetResendInterval,
    // Set/get max. number of frames to resend (arg: int32_t)
    // ---
    // This is the max. number of frames to request
    // in a single call to sink_handle_message().
    kAooCtlSetResendLimit,
    kAooCtlGetResendLimit,
    // Set/get redundancy (arg: int32_t)
    // ---
    // The number of times each frames is sent (default = 1)
    kAooCtlSetRedundancy,
    kAooCtlGetRedundancy,
    // Set/get source timeout in seconds (arg: AooSeconds)
    // ---
    // Time to wait before removing inactive source.
    kAooCtlSetSourceTimeout,
    kAooCtlGetSourceTimeout,
    // Set/get buffer fill ratio (arg: double)
    // ---
    // This is a read-only option for sinks which
    // gives a ratio of how full the buffer is;
    // 0.0 is empty and 1.0 is full
    kAooCtlGetBufferFillRatio,
    // Enable/disable binary data message (arg: AooBool)
    // ---
    // Use a more compact (and faster) binary format
    // for the audio data message
    kAooCtlSetBinaryDataMsg,
    kAooCtlGetBinaryDataMsg,
    // Set/get max. size of stream meta data (arg: AooInt32)
    // ---
    // Setting this property will allocate enough memory to
    // hold any stream metadata up to the given size.
    // Use this to avoid allocating memory in kAooCtlStartStream.
    kAooCtlSetStreamMetadataSize,
    kAooCtlGetStreamMetadataSize
};

//------------- user defined controls -----------------//

// User defined controls (for custom AOO versions)
// must start from kAooCtlUserDefined, for example:
//
// enum MyAooControls
// {
//     kMyControl1 = kAooCtlUserDefined
//     kMyControl2,
//     kMyControl3,
//     ...
// };
#define kAooCtlUserDefined 10000

//--------------- private controls ---------------------//

// Don't use! TODO: move somewhere else.
enum AooPrivateControls
{
    kAooCtlSetClient = -1000,
    kAooCtlNeedRelay
};
