//
// Created by sven on 04-08-25.
//

#pragma once

#include "AVCommon.h"

namespace ara::av {

enum SampleLimit {
    SignedInt16_Min = -32768,
    SignedInt16_Max = 32767,
    UnsignedInt16_Min = 0,
    UnsignedInt16_Max = 65535,
    SignedInt24_Min = -8388608,
    SignedInt24_Max = 8388607,
    UnsignedInt24_Min = 0,
    UnsignedInt24_Max = 16777215
};

template <typename SignedType>
typename std::make_unsigned<SignedType>::type convertSignedToUnsigned (SignedType signedValue) {
    static_assert (std::is_signed<SignedType>::value, "The input value must be signed");
    typename std::make_unsigned<SignedType>::type unsignedValue = static_cast<typename std::make_unsigned<SignedType>::type> (1) + std::numeric_limits<SignedType>::max();
    unsignedValue += signedValue;
    return unsignedValue;
}

template<typename T>
struct AudioSampleConverter {
    /** Convert a signed 8-bit integer to an audio sample */
    static T signedByteToSample(int8_t sample) {
        if constexpr (std::is_floating_point<T>::value) {
            return static_cast<T> (sample) / static_cast<T> (127.);
        } else if constexpr (std::numeric_limits<T>::is_integer) {
            if constexpr (std::is_signed_v<T>)
                return static_cast<T> (sample);
            else
                return static_cast<T> (convertSignedToUnsigned<int8_t> (sample));
        }
    }

    /** Convert an audio sample to an signed 8-bit representation */
    static int8_t sampleToSignedByte(T sample) {
        if constexpr (std::is_floating_point<T>::value) {
            sample = clamp (sample, -1., 1.);
            return static_cast<int8_t> (sample * (T)0x7F);
        } else {
            if constexpr (std::is_signed_v<T>)
                return static_cast<int8_t> (clamp (sample, -128, 127));
            else
                return static_cast<int8_t> (clamp (sample, 0, 255) - 128);
        }
    }

    /** Convert an unsigned 8-bit integer to an audio sample */
    static T unsignedByteToSample(uint8_t sample) {
        if constexpr (std::is_floating_point<T>::value) {
            return static_cast<T> (sample - 128) / static_cast<T> (127.);
        } else if (std::numeric_limits<T>::is_integer) {
            if constexpr (std::is_unsigned_v<T>)
                return static_cast<T> (sample);
            else
                return static_cast<T> (sample - 128);
        }
    }

    /** Convert an audio sample to an unsigned 8-bit representation */
    static uint8_t sampleToUnsignedByte(T sample) {
        if constexpr (std::is_floating_point<T>::value) {
            sample = clamp (sample, -1., 1.);
            sample = (sample + 1.) / 2.;
            return static_cast<uint8_t> (1 + (sample * 254));
        } else {
            if constexpr (std::is_signed_v<T>)
                return static_cast<uint8_t> (clamp (sample, -128, 127) + 128);
            else
                return static_cast<uint8_t> (clamp (sample, 0, 255));
        }
    }

    /** Convert a 16-bit integer to an audio sample */
    static T sixteenBitIntToSample(int16_t sample) {
        if constexpr (std::is_floating_point<T>::value) {
            return static_cast<T> (sample) / static_cast<T> (32767.);
        } else if constexpr (std::numeric_limits<T>::is_integer) {
            if constexpr (std::is_signed_v<T>)
                return static_cast<T> (sample);
            else
                return static_cast<T> (convertSignedToUnsigned<int16_t> (sample));
        }
    }

    /** Convert a an audio sample to a 16-bit integer */
    static int16_t sampleToSixteenBitInt(T sample) {
        if constexpr (std::is_floating_point<T>::value) {
            sample = clamp (sample, -1., 1.);
            return static_cast<int16_t> (sample * 32767.);
        } else {
            if constexpr (std::is_signed_v<T>)
                return static_cast<int16_t> (clamp (sample, SignedInt16_Min, SignedInt16_Max));
            else
                return static_cast<int16_t> (clamp (sample, UnsignedInt16_Min, UnsignedInt16_Max) + SignedInt16_Min);
        }
    }

    /** Convert a 24-bit value (int a 32-bit int) to an audio sample */
    static T twentyFourBitIntToSample(int32_t sample) {
        if constexpr (std::is_floating_point<T>::value) {
            return static_cast<T> (sample) / static_cast<T> (8388607.);
        } else if (std::numeric_limits<T>::is_integer) {
            if constexpr (std::is_signed_v<T>)
                return static_cast<T> (clamp (sample, SignedInt24_Min, SignedInt24_Max));
            else
                return static_cast<T> (clamp (sample + 8388608, UnsignedInt24_Min, UnsignedInt24_Max));
        }
    }

    /** Convert a an audio sample to a 24-bit value (in a 32-bit integer) */
    static int32_t sampleToTwentyFourBitInt(T sample) {
        if constexpr (std::is_floating_point<T>::value) {
            // multiplying a float by the max int32_t is problematic because
            // of rounding errors which can cause wrong values to come out, so
            // we use a different implementation here compared to other types
            if constexpr (std::is_same_v<T, float>) {
                if (sample >= 1.f)
                    return std::numeric_limits<int32_t>::max();
                else if (sample <= -1.f)
                    return std::numeric_limits<int32_t>::lowest() + 1; // starting at 1 preserves symmetry
                else
                    return static_cast<int32_t> (sample * std::numeric_limits<int32_t>::max());
            } else {
                return static_cast<int32_t> (clamp (sample, -1., 1.) * std::numeric_limits<int32_t>::max());
            }
        } else {
            if constexpr (std::is_signed_v<T>)
                return static_cast<int32_t> (clamp (sample, -2147483648LL, 2147483647LL));
            else
                return static_cast<int32_t> (clamp (sample, 0, 4294967295) - 2147483648);
        }
    }

    /** Convert a 32-bit signed integer to an audio sample */
    static T thirtyTwoBitIntToSample(int32_t sample) {
        if constexpr (std::is_floating_point<T>::value) {
            return static_cast<T> (sample) / static_cast<T> (std::numeric_limits<int32_t>::max());
        } else if (std::numeric_limits<T>::is_integer) {
            if constexpr (std::is_signed_v<T>)
                return static_cast<T> (sample);
            else
                return static_cast<T> (clamp (static_cast<T> (sample + 2147483648), 0, 4294967295));
        }
    }

    /** Convert a an audio sample to a 32-bit signed integer */
    static int32_t sampleToThirtyTwoBitInt(T sample)  {
        if constexpr (std::is_floating_point<T>::value) {
            sample = clamp (sample, -1., 1.);
            return static_cast<int32_t> (sample * 8388607.);
        } else {
            if constexpr (std::is_signed_v<T>)
                return static_cast<int32_t> (clamp (sample, SignedInt24_Min, SignedInt24_Max));
            else
                return static_cast<int32_t> (clamp (sample, UnsignedInt24_Min, UnsignedInt24_Max) + SignedInt24_Min);
        }
    }

    /** Helper clamp function to enforce ranges */
    static T clamp(T value, T minValue, T maxValue) {
        value = std::min (value, maxValue);
        value = std::max (value, minValue);
        return value;
    }
};

}