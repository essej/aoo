/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */


#include "aoo_common.hpp"

#include "common/utils.hpp"

#include "codec/aoo_null.h"
#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
#include "codec/aoo_opus.h"
#endif

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#define CLAMP(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))

/*/////////////////////////// helper functions ///////////////////////////////////////*/

int address_to_atoms(const aoo::ip_address& addr, int argc, t_atom *a)
{
    if (argc < 2){
        return 0;
    }
    SETSYMBOL(a, gensym(addr.name()));
    SETFLOAT(a + 1, addr.port());
    return 2;
}

int endpoint_to_atoms(const aoo::ip_address& addr, AooId id, int argc, t_atom *argv)
{
    if (argc < 3 || !addr.valid()){
        return 0;
    }
    SETSYMBOL(argv, gensym(addr.name()));
    SETFLOAT(argv + 1, addr.port());
    SETFLOAT(argv + 2, id);
    return 3;
}

static int32_t format_getparam(void *x, int argc, t_atom *argv, int which,
                               const char *name, int32_t def)
{
    if (argc > which){
        if (argv[which].a_type == A_FLOAT){
            return argv[which].a_w.w_float;
        }
    #if 1
        t_symbol *s = atom_getsymbol(argv + which);
        if (s != gensym("_")){
            pd_error(x, "%s: bad %s argument (%s), using %d", classname(x), name, s->s_name, def);
        }
    #endif
    }
    return def;
}

bool format_parse(t_pd *x, AooFormatStorage &f, int argc, t_atom *argv,
                  int defchannels, int defsr, int defblocksize)
{
    t_symbol *codec = atom_getsymbolarg(0, argc, argv);

    // in case the "dsp" method has not been called yet
    if (defchannels <= 0) {
        defchannels = 1;
    }
    if (defsr <= 0) {
        defsr = sys_getsr();
    }
    if (defblocksize <= 0) {
        defblocksize = sys_getblksize();
    }

    if (codec == gensym(kAooCodecNull)){
        // null <channels> <blocksize> <samplerate>
        auto numchannels = format_getparam(x, argc, argv, 1, "channels", 1);
        auto blocksize = format_getparam(x, argc, argv, 2, "blocksize", defblocksize);
        auto samplerate = format_getparam(x, argc, argv, 3, "samplerate", defsr);

        AooFormatNull_init((AooFormatNull *)&f.header, numchannels, samplerate, blocksize);
    } else if (codec == gensym(kAooCodecPcm)){
        // pcm <channels> <blocksize> <samplerate> <bitdepth>
        auto numchannels = format_getparam(x, argc, argv, 1, "channels", defchannels);
        auto blocksize = format_getparam(x, argc, argv, 2, "blocksize", defblocksize);
        auto samplerate = format_getparam(x, argc, argv, 3, "samplerate", defsr);

        auto nbits = format_getparam(x, argc, argv, 4, "bitdepth", 4);
        AooPcmBitDepth bitdepth;
        switch (nbits){
        case 1:
            bitdepth =  kAooPcmInt8;
            break;
        case 2:
            bitdepth = kAooPcmInt16;
            break;
        case 3:
            bitdepth = kAooPcmInt24;
            break;
        case 4:
            bitdepth = kAooPcmFloat32;
            break;
        case 8:
            bitdepth = kAooPcmFloat64;
            break;
        default:
            pd_error(x, "%s: bad bitdepth argument %d", classname(x), nbits);
            return false;
        }

        AooFormatPcm_init((AooFormatPcm *)&f.header, numchannels, samplerate, blocksize, bitdepth);
    }
#if AOO_USE_OPUS
    else if (codec == gensym(kAooCodecOpus)){
        // opus <channels> <blocksize> <samplerate> <application>
        opus_int32 numchannels = format_getparam(x, argc, argv, 1, "channels", defchannels);
        opus_int32 blocksize = format_getparam(x, argc, argv, 2, "blocksize", 480); // 10ms
        opus_int32 samplerate = format_getparam(x, argc, argv, 3, "samplerate", 48000);

        // application type ("auto", "audio", "voip", "lowdelay"
        opus_int32 applicationType;
        if (argc > 4){
            std::string_view type = atom_getsymbol(argv + 4)->s_name;
            if (type == "_" || type == "audio") {
                applicationType = OPUS_APPLICATION_AUDIO;
            } else if (type == "voip"){
                applicationType = OPUS_APPLICATION_VOIP;
            } else if (type == "lowdelay") {
                applicationType = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
            } else {
                pd_error(x,"%s: unsupported application type '%s'",
                         classname(x), type.data());
                return false;
            }
        } else {
            applicationType = OPUS_APPLICATION_AUDIO;
        }

        AooFormatOpus_init((AooFormatOpus *)&f.header, numchannels,
                           samplerate, blocksize, applicationType);
    }
#endif
    else {
        pd_error(x, "%s: unknown codec '%s'", classname(x), codec->s_name);
        return false;
    }
    return true;
}

int format_to_atoms(const AooFormat &f, int argc, t_atom *argv)
{
    if (argc < 4){
        bug("format_to_atoms: too few atoms!");
        return 0;
    }
    t_symbol *codec = gensym(f.codecName);
    SETSYMBOL(argv, codec);
    SETFLOAT(argv + 1, f.numChannels);
    SETFLOAT(argv + 2, f.blockSize);
    SETFLOAT(argv + 3, f.sampleRate);

    if (codec == gensym(kAooCodecNull)){
        // null <channels> <blocksize> <samplerate>
        return 4;
    } else if (codec == gensym(kAooCodecPcm)){
        // pcm <channels> <blocksize> <samplerate> <bitdepth>
        if (argc < 5){
            bug("format_to_atoms: too few atoms for pcm format!");
            return 0;
        }
        auto& fmt = (AooFormatPcm &)f;
        int nbytes;
        switch (fmt.bitDepth){
        case kAooPcmInt8:
            nbytes = 1;
            break;
        case kAooPcmInt16:
            nbytes = 2;
            break;
        case kAooPcmInt24:
            nbytes = 3;
            break;
        case kAooPcmFloat32:
            nbytes = 4;
            break;
        case kAooPcmFloat64:
            nbytes = 8;
            break;
        default:
            pd_error(0, "format_to_atoms: bad bitdepth argument %d", fmt.bitDepth);
            return 0;
        }
        SETFLOAT(argv + 4, nbytes);
        return 5;
    }
#if AOO_USE_OPUS
    else if (codec == gensym(kAooCodecOpus)){
        // opus <channels> <blocksize> <samplerate> <application>
        if (argc < 5){
            bug("format_to_atoms: too few atoms for opus format!");
            return 0;
        }

        auto& fmt = (AooFormatOpus &)f;

        // application type
        t_symbol *type;
        switch (fmt.applicationType){
        case OPUS_APPLICATION_VOIP:
            type = gensym("voip");
            break;
        case OPUS_APPLICATION_RESTRICTED_LOWDELAY:
            type = gensym("lowdelay");
            break;
        case OPUS_APPLICATION_AUDIO:
            type = gensym("audio");
            break;
        default:
            pd_error(0, "format_to_atoms: bad application type argument %d",
                  fmt.applicationType);
            return 0;
        }
        SETSYMBOL(argv + 4, type);

        return 5;
    }
#endif
    else {
        pd_error(0, "format_to_atoms: unknown format %s!", codec->s_name);
    }
    return 0;
}

bool atom_to_datatype(const t_atom &a, AooDataType& type, void *x) {
    if (a.a_type == A_SYMBOL) {
        auto str = a.a_w.w_symbol->s_name;
        auto result = aoo_dataTypeFromString(str);
        if (result != kAooDataUnspecified) {
            type = result;
            return true;
        } else if (!strcmp(str, "f")) {
            if (sizeof(t_float) == 8) {
                type = kAooDataFloat64;
            } else {
                type = kAooDataFloat32;
            }
            return true;
        } else {
            pd_error(x, "%s: unknown data type '%s'", classname(x), str);
        }
    } else {
        pd_error(x, "%s: bad metadata type", classname(x));
    }
    return false;
}

int datatype_element_size(AooDataType type) {
    switch (type) {
    case kAooDataFloat32:
        return 4;
    case kAooDataFloat64:
        return 8;
    case kAooDataInt16:
        return 2;
    case kAooDataInt32:
        return 4;
    case kAooDataInt64:
        return 8;
    default:
        return 1;
    }
}

int data_to_atoms(const AooData& data, int argc, t_atom *argv) {
    assert(data.size != 0);
    auto numatoms = data.size / datatype_element_size(data.type) + 1;
    if (numatoms > argc) {
        return 0;
    }

    switch (data.type) {
    case kAooDataFloat32:
    case kAooDataFloat64:
    case kAooDataInt16:
    case kAooDataInt32:
    case kAooDataInt64:
        SETSYMBOL(argv, gensym("f"));
        break;
    default:
        SETSYMBOL(argv, gensym(aoo_dataTypeToString(data.type)));
        break;
    }

    auto ptr = data.data;
    for (int i = 1; i < numatoms; ++i) {
        t_floatarg f;
        switch (data.type) {
        case kAooDataFloat32:
            f = aoo::read_bytes<float>(ptr);
            break;
        case kAooDataFloat64:
            f = aoo::read_bytes<double>(ptr);
            break;
        case kAooDataInt16:
            f = aoo::read_bytes<int16_t>(ptr);
            break;
        case kAooDataInt32:
            f = aoo::read_bytes<int32_t>(ptr);
            break;
        case kAooDataInt64:
            f = aoo::read_bytes<int64_t>(ptr);
            break;
        default:
            f = *ptr++;
            break;
        }
        SETFLOAT(argv + i, f);
    }
    assert(ptr <= (data.data + data.size));
    return numatoms;
}

int stream_message_to_atoms(const AooStreamMessage& msg, int argc, t_atom *argv) {
    assert(argc > 2);
    SETFLOAT(argv, msg.channel);
    AooData data { msg.type, msg.data, (AooSize)msg.size };
    return data_to_atoms(data, argc - 1, argv + 1) + 1;
}

int atoms_to_data(AooDataType type, int argc, const t_atom *argv,
                  AooByte *data, AooSize size) {
    auto numbytes = argc * datatype_element_size(type);
    if (numbytes > size) {
        return 0;
    }
    auto ptr = data;
    for (int i = 0; i < argc; ++i) {
        auto f = atom_getfloat(argv + i);
        switch (type) {
        case kAooDataFloat32:
            aoo::write_bytes<float>(f, ptr);
            break;
        case kAooDataFloat64:
            aoo::write_bytes<double>(f, ptr);
            break;
        case kAooDataInt16:
            aoo::write_bytes<int16_t>(f, ptr);
            break;
        case kAooDataInt32:
            aoo::write_bytes<int32_t>(f, ptr);
            break;
        case kAooDataInt64:
            aoo::write_bytes<int64_t>(f, ptr);
            break;
        default:
            *ptr++ = (AooByte)f;
            break;
        }
    }
    assert(ptr - data <= size);
    return numbytes;
}
