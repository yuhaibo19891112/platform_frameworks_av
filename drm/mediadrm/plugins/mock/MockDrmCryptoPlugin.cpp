/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "MockDrmCryptoPlugin"
#include <utils/Log.h>


#include "drm/DrmAPI.h"
#include "MockDrmCryptoPlugin.h"

using namespace android;

// Shared library entry point
DrmFactory *createDrmFactory()
{
    return new MockDrmFactory();
}

// Shared library entry point
CryptoFactory *createCryptoFactory()
{
    return new MockCryptoFactory();
}

const uint8_t mock_uuid[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                               0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};

namespace android {

    // MockDrmFactory
    bool MockDrmFactory::isCryptoSchemeSupported(const uint8_t uuid[16])
    {
        return (!memcmp(uuid, mock_uuid, sizeof(uuid)));
    }

    status_t MockDrmFactory::createDrmPlugin(const uint8_t uuid[16], DrmPlugin **plugin)
    {
        *plugin = new MockDrmPlugin();
        return OK;
    }

    // MockCryptoFactory
    bool MockCryptoFactory::isCryptoSchemeSupported(const uint8_t uuid[16]) const
    {
        return (!memcmp(uuid, mock_uuid, sizeof(uuid)));
    }

    status_t MockCryptoFactory::createPlugin(const uint8_t uuid[16], const void *data,
                                             size_t size, CryptoPlugin **plugin)
    {
        *plugin = new MockCryptoPlugin();
        return OK;
    }


    // MockDrmPlugin methods

    status_t MockDrmPlugin::openSession(Vector<uint8_t> &sessionId)
    {
        const size_t kSessionIdSize = 8;

        Mutex::Autolock lock(mLock);
        for (size_t i = 0; i < kSessionIdSize / sizeof(long); i++) {
            long r = random();
            sessionId.appendArray((uint8_t *)&r, sizeof(long));
        }
        mSessions.add(sessionId);

        ALOGD("MockDrmPlugin::openSession() -> %s", vectorToString(sessionId).string());
        return OK;
    }

    status_t MockDrmPlugin::closeSession(Vector<uint8_t> const &sessionId)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::closeSession(%s)", vectorToString(sessionId).string());
        ssize_t index = findSession(sessionId);
        if (index == kNotFound) {
            ALOGD("Invalid sessionId");
            return BAD_VALUE;
        }
        mSessions.removeAt(index);
        return OK;
    }


    status_t MockDrmPlugin::getLicenseRequest(Vector<uint8_t> const &sessionId,
                                              Vector<uint8_t> const &initData,
                                              String8 const &mimeType, LicenseType licenseType,
                                              KeyedVector<String8, String8> const &optionalParameters,
                                              Vector<uint8_t> &request, String8 &defaultUrl)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::getLicenseRequest(sessionId=%s, initData=%s, mimeType=%s"
              ", licenseType=%d, optionalParameters=%s))",
              vectorToString(sessionId).string(), vectorToString(initData).string(), mimeType.string(),
              licenseType, stringMapToString(optionalParameters).string());

        ssize_t index = findSession(sessionId);
        if (index == kNotFound) {
            ALOGD("Invalid sessionId");
            return BAD_VALUE;
        }

        // Properties used in mock test, set by mock plugin and verifed cts test app
        //   byte[] initData           -> mock-initdata
        //   string mimeType           -> mock-mimetype
        //   string licenseType        -> mock-licensetype
        //   string optionalParameters -> mock-optparams formatted as {key1,value1},{key2,value2}

        mByteArrayProperties.add(String8("mock-initdata"), initData);
        mStringProperties.add(String8("mock-mimetype"), mimeType);

        String8 licenseTypeStr;
        licenseTypeStr.appendFormat("%d", (int)licenseType);
        mStringProperties.add(String8("mock-licensetype"), licenseTypeStr);

        String8 params;
        for (size_t i = 0; i < optionalParameters.size(); i++) {
            params.appendFormat("%s{%s,%s}", i ? "," : "",
                                optionalParameters.keyAt(i).string(),
                                optionalParameters.valueAt(i).string());
        }
        mStringProperties.add(String8("mock-optparams"), params);

        // Properties used in mock test, set by cts test app returned from mock plugin
        //   byte[] mock-request       -> request
        //   string mock-default-url   -> defaultUrl

        index = mByteArrayProperties.indexOfKey(String8("mock-request"));
        if (index < 0) {
            ALOGD("Missing 'mock-request' parameter for mock");
            return BAD_VALUE;
        } else {
            request = mByteArrayProperties.valueAt(index);
        }

        index = mStringProperties.indexOfKey(String8("mock-defaultUrl"));
        if (index < 0) {
            ALOGD("Missing 'mock-defaultUrl' parameter for mock");
            return BAD_VALUE;
        } else {
            defaultUrl = mStringProperties.valueAt(index);
        }
        return OK;
    }

    status_t MockDrmPlugin::provideLicenseResponse(Vector<uint8_t> const &sessionId,
                                                   Vector<uint8_t> const &response)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::provideLicenseResponse(sessionId=%s, response=%s)",
              vectorToString(sessionId).string(), vectorToString(response).string());
        ssize_t index = findSession(sessionId);
        if (index == kNotFound) {
            ALOGD("Invalid sessionId");
            return BAD_VALUE;
        }
        if (response.size() == 0) {
            return BAD_VALUE;
        }

        // Properties used in mock test, set by mock plugin and verifed cts test app
        //   byte[] response            -> mock-response

        mByteArrayProperties.add(String8("mock-response"), response);

        return OK;
    }

    status_t MockDrmPlugin::removeLicense(Vector<uint8_t> const &sessionId)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::removeLicense(sessionId=%s)",
              vectorToString(sessionId).string());
        ssize_t index = findSession(sessionId);
        if (index == kNotFound) {
            ALOGD("Invalid sessionId");
            return BAD_VALUE;
        }

        return OK;
    }

    status_t MockDrmPlugin::queryLicenseStatus(Vector<uint8_t> const &sessionId,
                                               KeyedVector<String8, String8> &infoMap) const
    {
        ALOGD("MockDrmPlugin::queryLicenseStatus(sessionId=%s)",
              vectorToString(sessionId).string());

        ssize_t index = findSession(sessionId);
        if (index == kNotFound) {
            ALOGD("Invalid sessionId");
            return BAD_VALUE;
        }

        infoMap.add(String8("purchaseDuration"), String8("1000"));
        infoMap.add(String8("licenseDuration"), String8("100"));
        return OK;
    }

    status_t MockDrmPlugin::getProvisionRequest(Vector<uint8_t> &request,
                                                String8 &defaultUrl)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::getProvisionRequest()");

        // Properties used in mock test, set by cts test app returned from mock plugin
        //   byte[] mock-request       -> request
        //   string mock-default-url   -> defaultUrl

        ssize_t index = mByteArrayProperties.indexOfKey(String8("mock-request"));
        if (index < 0) {
            ALOGD("Missing 'mock-request' parameter for mock");
            return BAD_VALUE;
        } else {
            request = mByteArrayProperties.valueAt(index);
        }

        index = mStringProperties.indexOfKey(String8("mock-defaultUrl"));
        if (index < 0) {
            ALOGD("Missing 'mock-defaultUrl' parameter for mock");
            return BAD_VALUE;
        } else {
            defaultUrl = mStringProperties.valueAt(index);
        }
        return OK;
    }

    status_t MockDrmPlugin::provideProvisionResponse(Vector<uint8_t> const &response)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::provideProvisionResponse(%s)",
              vectorToString(response).string());

        // Properties used in mock test, set by mock plugin and verifed cts test app
        //   byte[] response            -> mock-response

        mByteArrayProperties.add(String8("mock-response"), response);
        return OK;
    }

    status_t MockDrmPlugin::getSecureStops(List<Vector<uint8_t> > &secureStops)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::getSecureStops()");
        const uint8_t ss1[] = {0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89};
        const uint8_t ss2[] = {0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99};

        Vector<uint8_t> vec;
        vec.appendArray(ss1, sizeof(ss1));
        secureStops.push_back(vec);

        vec.clear();
        vec.appendArray(ss2, sizeof(ss2));
        secureStops.push_back(vec);
        return OK;
    }

    status_t MockDrmPlugin::releaseSecureStops(Vector<uint8_t> const &ssRelease)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::releaseSecureStops(%s)",
              vectorToString(ssRelease).string());
        return OK;
    }

    status_t MockDrmPlugin::getPropertyString(String8 const &name, String8 &value) const
    {
        ALOGD("MockDrmPlugin::getPropertyString(name=%s)", name.string());
        ssize_t index = mStringProperties.indexOfKey(name);
        if (index < 0) {
            ALOGD("no property for '%s'", name.string());
            return BAD_VALUE;
        }
        value = mStringProperties.valueAt(index);
        return OK;
    }

    status_t MockDrmPlugin::getPropertyByteArray(String8 const &name,
                                                 Vector<uint8_t> &value) const
    {
        ALOGD("MockDrmPlugin::getPropertyByteArray(name=%s)", name.string());
        ssize_t index = mByteArrayProperties.indexOfKey(name);
        if (index < 0) {
            ALOGD("no property for '%s'", name.string());
            return BAD_VALUE;
        }
        value = mByteArrayProperties.valueAt(index);
        return OK;
    }

    status_t MockDrmPlugin::setPropertyString(String8 const &name,
                                              String8 const &value)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::setPropertyString(name=%s, value=%s)",
              name.string(), value.string());
        mStringProperties.add(name, value);
        return OK;
    }

    status_t MockDrmPlugin::setPropertyByteArray(String8 const &name,
                                                 Vector<uint8_t> const &value)
    {
        Mutex::Autolock lock(mLock);
        ALOGD("MockDrmPlugin::setPropertyByteArray(name=%s, value=%s)",
              name.string(), vectorToString(value).string());
        mByteArrayProperties.add(name, value);
        return OK;
    }

    ssize_t MockDrmPlugin::findSession(Vector<uint8_t> const &sessionId) const
    {
        ALOGD("findSession: nsessions=%d, size=%d", mSessions.size(), sessionId.size());
        for (size_t i = 0; i < mSessions.size(); ++i) {
            if (memcmp(mSessions[i].array(), sessionId.array(), sessionId.size()) == 0) {
                return i;
            }
        }
        return kNotFound;
    }

    // Conversion utilities
    String8 MockDrmPlugin::vectorToString(Vector<uint8_t> const &vector) const
    {
        return arrayToString(vector.array(), vector.size());
    }

    String8 MockDrmPlugin::arrayToString(uint8_t const *array, size_t len) const
    {
        String8 result("{ ");
        for (size_t i = 0; i < len; i++) {
            result.appendFormat("0x%02x ", array[i]);
        }
        result += "}";
        return result;
    }

    String8 MockDrmPlugin::stringMapToString(KeyedVector<String8, String8> map) const
    {
        String8 result("{ ");
        for (size_t i = 0; i < map.size(); i++) {
            result.appendFormat("%s{name=%s, value=%s}", i > 0 ? ", " : "",
                                map.keyAt(i).string(), map.valueAt(i).string());
        }
        return result + " }";
    }

    bool operator<(Vector<uint8_t> const &lhs, Vector<uint8_t> const &rhs) {
        return lhs.size() < rhs.size() || (memcmp(lhs.array(), rhs.array(), lhs.size()) < 0);
    }

    //
    // Crypto Plugin
    //

    bool MockCryptoPlugin::requiresSecureDecoderComponent(const char *mime) const
    {
        ALOGD("MockCryptoPlugin::requiresSecureDecoderComponent(mime=%s)", mime);
        return false;
    }

    ssize_t
    MockCryptoPlugin::decrypt(bool secure, const uint8_t key[16], const uint8_t iv[16],
                              Mode mode, const void *srcPtr, const SubSample *subSamples,
                              size_t numSubSamples, void *dstPtr, AString *errorDetailMsg)
    {
        ALOGD("MockCryptoPlugin::decrypt(secure=%d, key=%s, iv=%s, mode=%d, src=%p, "
              "subSamples=%s, dst=%p)",
              (int)secure,
              arrayToString(key, sizeof(key)).string(),
              arrayToString(iv, sizeof(iv)).string(),
              (int)mode, srcPtr,
              subSamplesToString(subSamples, numSubSamples).string(),
              dstPtr);
        return OK;
    }

    // Conversion utilities
    String8 MockCryptoPlugin::arrayToString(uint8_t const *array, size_t len) const
    {
        String8 result("{ ");
        for (size_t i = 0; i < len; i++) {
            result.appendFormat("0x%02x ", array[i]);
        }
        result += "}";
        return result;
    }

    String8 MockCryptoPlugin::subSamplesToString(SubSample const *subSamples,
                                                 size_t numSubSamples) const
    {
        String8 result;
        for (size_t i = 0; i < numSubSamples; i++) {
            result.appendFormat("[%d] {clear:%d, encrypted:%d} ", i,
                                subSamples[i].mNumBytesOfClearData,
                                subSamples[i].mNumBytesOfEncryptedData);
        }
        return result;
    }

};