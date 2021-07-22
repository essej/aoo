#pragma once

#include "aoo_defines.h"

AOO_PACK_BEGIN

//--------------------------------//

#define kAooCodecMaxSettingSize 256

typedef void* (AOO_CALL *AooCodecNewFunc)(AooError *);

typedef void (AOO_CALL *AooCodecFreeFunc)(void *);

// encode samples to bytes
typedef AooError (AOO_CALL *AooCodecEncodeFunc)(
        void *,             // the encoder instance
        const AooSample *,  // input samples (interleaved)
        AooInt32,           // number of samples
        AooByte *,          // output buffer
        AooInt32 *          // max. buffer size (updated to actual size)
);

// decode bytes to samples
typedef AooError (AOO_CALL *AooCodecDecodeFunc)(
        void *,             // the decoder instance
        const AooByte *,    // input bytes
        AooInt32,           // input size
        AooSample *,        // output samples (interleaved)
        AooInt32 *          // max. number of samples (updated to actual number)

);

// serialize format options (everything after the 'AooFormat' header)
typedef AooError (AOO_CALL *AooCodecSerializeFunc)(
        const AooFormat *,  // source format
        AooByte *,          // option buffer
        AooInt32 *          // buffer size (updated to actual size)
);

// deserialize format options (everything after the 'AooFormat' header).
typedef AooError (AOO_CALL *AooCodecDeserializeFunc)(
        const AooFormat *,  // format header
        const AooByte *,    // option buffer
        AooInt32,           // buffer size
        AooFormat *,        // format buffer large enough to hold the codec format.
        AooInt32            // size of the format buffer
);

typedef AooInt32 AooCodecCtl;

// NOTE: codec specific controls are assumed to be positiv, e.g. OPUS_SET_BITRATE.
enum AooCodecControls
{
    // reset the codec state (NULL)
    kAooCodecCtlReset = -1000,
    // set the codec format (AooFormat)
    // ---
    // Set the format by passing the format header.
    // The format struct is validated and updated on success!
    kAooCodecCtlSetFormat,
    // get the codec format (AooFormat)
    // ---
    // Get the format by passing an instance of 'AooFormatStorage'
    // or a similar struct that is large enough to hold any format.
    // On success, the actual format size will be contained in the
    // 'size' member of the format header.
    kAooCodecCtlGetFormat,
    // check if the format is equal (AooFormat)
    // ---
    // returns kAooTrue or kAooFalse
    kAooCodecCtlFormatEqual
};

// codec control
typedef AooError (AOO_CALL *AooCodecControlFunc)
(
        void *,         // the encoder/decoder instance
        AooCodecCtl,    // the ctl number
        void *,         // pointer to value
        AooSize         // the value size
);

typedef AOO_STRUCT AooCodecInterface
{
    // encoder
    AooCodecNewFunc encoderNew;
    AooCodecFreeFunc encoderFree;
    AooCodecControlFunc encoderControl;
    AooCodecEncodeFunc encoderEncode;
    // decoder
    AooCodecNewFunc decoderNew;
    AooCodecFreeFunc decoderFree;
    AooCodecControlFunc decoderControl;
    AooCodecDecodeFunc decoderDecode;
    // helpers
    AooCodecSerializeFunc serialize;
    AooCodecDeserializeFunc deserialize;
    void *future;
} AooCodecInterface;

// register an external codec plugin
AOO_API AooError AOO_CALL aoo_registerCodec(
        const AooChar *name, const AooCodecInterface *codec);

// The type of 'aoo_registerCodec', which gets passed to codec plugins
// to register themselves.
typedef AooError (AOO_CALL *AooCodecRegisterFunc)
        (const AooChar *, const AooCodecInterface *);

// NOTE: AOO doesn't support dynamic plugin loading out of the box,
// but it is quite easy to implement on your own.
// You just have to put one or more codecs in a shared library and export
// a single function of type AooCodecSetupFunc with the name 'aoo_setup':
//
// void aoo_setup(AooCodecRegisterFunc fn, AooLogFunc log, const AooAllocator *alloc);
//
// In your host application, you would then scan directories for shared libraries,
// check if they export a function named 'aoo_setup', and if yes, called it with
// a pointer to 'aoo_registerCodec' and (optionally) the log function and custom allocator.
typedef AooError (AOO_CALL *AooCodecSetupFunc)
        (const AooCodecRegisterFunc *, AooLogFunc, const AooAllocator *);

//--------------------------------//

AOO_PACK_END
