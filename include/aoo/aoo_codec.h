#pragma once

#include "aoo_defines.h"

AOO_PACK_BEGIN

//--------------------------------//

typedef struct AooCodec
{
    struct AooCodecInterface *interface;
} AooCodec;

typedef AooCodec * (AOO_CALL *AooCodecNewFunc)(
        AooFormat *format,  // the desired format; validated and updated on success
        AooError *error     // error code on failure (result is NULL)
);

typedef void (AOO_CALL *AooCodecFreeFunc)(AooCodec *);

// encode samples to bytes
typedef AooError (AOO_CALL *AooCodecEncodeFunc)(
        AooCodec *encoder,          // the encoder instance
        const AooSample *inSamples, // input samples (interleaved)
        AooInt32 numSamples,        // number of samples
        AooByte *outBytes,          // output buffer
        AooInt32 *numBytes          // max. buffer size (updated to actual size)
);

// decode bytes to samples
typedef AooError (AOO_CALL *AooCodecDecodeFunc)(
        AooCodec *decoder,      // the decoder instance
        const AooByte *inBytes, // input bytes
        AooInt32 numBytes,      // input size
        AooSample *outSamples,  // output samples (interleaved)
        AooInt32 *numSamples    // max. number of samples (updated to actual number)

);

typedef AooInt32 AooCodecCtl;

// negative values are reserved for generic controls;
// codec specific controls must be positiv
enum AooCodecControls
{
    // reset the codec state (NULL)
    kAooCodecCtlReset = -1000
};

// codec control
typedef AooError (AOO_CALL *AooCodecControlFunc)
(
        AooCodec *codec,    // the encoder/decoder instance
        AooCodecCtl ctl,    // the ctl number
        void *data,         // pointer to value
        AooSize size        // the value size
);

// serialize format extension (everything after the 'AooFormat' header).
// on success writes the format extension to the given buffer
typedef AooError (AOO_CALL *AooCodecSerializeFunc)(
        const AooFormat *format,    // source format
        AooByte *buffer,            // extension buffer; NULL: return the required buffer size.
        AooInt32 *bufsize           // max. buffer size (updated to actual resp. required size)
);

// deserialize format extension (everything after the 'AooFormat' header).
// on success writes the format extension to the given format structure
typedef AooError (AOO_CALL *AooCodecDeserializeFunc)(
        const AooByte *buffer,  // extension buffer
        AooInt32 bufsize   ,    // buffer size
        AooFormat *format,      // destination format structure; NULL: return the required format size
        AooInt32 *fmtsize       // max. format size (updated to actual resp. required size)
);
// NOTE: this function does *not* automatically update the 'size' member of the format structure,
// but you can simply point the last argument to it.

typedef struct AooCodecInterface
{
    // encoder methods
    AooCodecNewFunc encoderNew;
    AooCodecFreeFunc encoderFree;
    AooCodecControlFunc encoderControl;
    AooCodecEncodeFunc encoderEncode;
    // decoder methods
    AooCodecNewFunc decoderNew;
    AooCodecFreeFunc decoderFree;
    AooCodecControlFunc decoderControl;
    AooCodecDecodeFunc decoderDecode;
    // free functions
    AooCodecSerializeFunc serialize;
    AooCodecDeserializeFunc deserialize;
    void *future;
} AooCodecInterface;

//---------------- helper functions ---------------------//

AOO_INLINE AooError AooEncoder_encode(AooCodec *enc,
                           const AooSample *input, AooInt32 numSamples,
                           AooByte *output, AooInt32 *numBytes) {
    return enc->interface->encoderEncode(enc, input, numSamples, output, numBytes);
}

AOO_INLINE AooError AooEncoder_control(AooCodec *enc, AooCodecCtl ctl, void *data, AooSize size) {
    return enc->interface->encoderControl(enc, ctl, data, size);
}

AOO_INLINE AooError AooEncoder_reset(AooCodec *enc) {
    return enc->interface->encoderControl(enc, kAooCodecCtlReset, NULL, 0);
}

AOO_INLINE AooError AooDecoder_decode(AooCodec *dec,
                           const AooByte *input, AooInt32 numBytes,
                           AooSample *output, AooInt32 *numSamples) {
    return dec->interface->decoderDecode(dec, input, numBytes, output, numSamples);
}

AOO_INLINE AooError AooDecoder_control(AooCodec *dec, AooCodecCtl ctl, void *data, AooSize size) {
    return dec->interface->decoderControl(dec, ctl, data, size);
}

AOO_INLINE AooError AooDecoder_reset(AooCodec *dec) {
    return dec->interface->encoderControl(dec, kAooCodecCtlReset, NULL, 0);
}

//---------------- register codecs ----------------------//

// register an external codec plugin
AOO_API AooError AOO_CALL aoo_registerCodec(
        const AooChar *name, const AooCodecInterface *codec);

// The type of 'aoo_registerCodec', which gets passed to codec plugins
// to register themselves.
typedef AooError (AOO_CALL *AooCodecRegisterFunc)(
        const AooChar *name,                // codec name
        const AooCodecInterface *interface  // codec interface
);

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
        (const AooCodecRegisterFunc *registerFunction,
         AooLogFunc logFunction, const AooAllocator *allocator);

//--------------------------------//

AOO_PACK_END
