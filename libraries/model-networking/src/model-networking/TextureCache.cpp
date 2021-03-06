//
//  TextureCache.cpp
//  libraries/model-networking/src
//
//  Created by Andrzej Kapolka on 8/6/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TextureCache.h"

#include <mutex>

#include <QNetworkReply>
#include <QPainter>
#include <QRunnable>
#include <QThreadPool>
#include <QImageReader>

#if DEBUG_DUMP_TEXTURE_LOADS
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>

#include <gpu/Batch.h>

#include <ktx/KTX.h>

#include <NumericalConstants.h>
#include <shared/NsightHelpers.h>

#include <Finally.h>

#include "ModelNetworkingLogging.h"
#include <Trace.h>
#include <StatTracker.h>

Q_LOGGING_CATEGORY(trace_resource_parse_image, "trace.resource.parse.image")
Q_LOGGING_CATEGORY(trace_resource_parse_image_raw, "trace.resource.parse.image.raw")
Q_LOGGING_CATEGORY(trace_resource_parse_image_ktx, "trace.resource.parse.image.ktx")

const std::string TextureCache::KTX_DIRNAME { "ktx_cache" };
const std::string TextureCache::KTX_EXT { "ktx" };

TextureCache::TextureCache() :
    _ktxCache(KTX_DIRNAME, KTX_EXT) {
    setUnusedResourceCacheSize(0);
    setObjectName("TextureCache");

    // Expose enum Type to JS/QML via properties
    // Despite being one-off, this should be fine, because TextureCache is a SINGLETON_DEPENDENCY
    QObject* type = new QObject(this);
    type->setObjectName("TextureType");
    setProperty("Type", QVariant::fromValue(type));
    auto metaEnum = QMetaEnum::fromType<Type>();
    for (int i = 0; i < metaEnum.keyCount(); ++i) {
        type->setProperty(metaEnum.key(i), metaEnum.value(i));
    }
}

TextureCache::~TextureCache() {
}

// use fixed table of permutations. Could also make ordered list programmatically
// and then shuffle algorithm. For testing, this ensures consistent behavior in each run.
// this list taken from Ken Perlin's Improved Noise reference implementation (orig. in Java) at
// http://mrl.nyu.edu/~perlin/noise/

const int permutation[256] =
{
    151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
    140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
    247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
     57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
     74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
     60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
     65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
     200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186,   3,  64,
     52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
    207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
    119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
    129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
    218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
     81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
    184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
    222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180
};

#define USE_CHRIS_NOISE 1

const gpu::TexturePointer& TextureCache::getPermutationNormalTexture() {
    if (!_permutationNormalTexture) {

        // the first line consists of random permutation offsets
        unsigned char data[256 * 2 * 3];
#if (USE_CHRIS_NOISE==1)
        for (int i = 0; i < 256; i++) {
            data[3*i+0] = permutation[i];
            data[3*i+1] = permutation[i];
            data[3*i+2] = permutation[i];
        }
#else
        for (int i = 0; i < 256 * 3; i++) {
            data[i] = rand() % 256;
        }
#endif

        for (int i = 256 * 3; i < 256 * 3 * 2; i += 3) {
            glm::vec3 randvec = glm::sphericalRand(1.0f);
            data[i] = ((randvec.x + 1.0f) / 2.0f) * 255.0f;
            data[i + 1] = ((randvec.y + 1.0f) / 2.0f) * 255.0f;
            data[i + 2] = ((randvec.z + 1.0f) / 2.0f) * 255.0f;
        }

        _permutationNormalTexture = gpu::TexturePointer(gpu::Texture::create2D(gpu::Element(gpu::VEC3, gpu::NUINT8, gpu::RGB), 256, 2));
        _permutationNormalTexture->setStoredMipFormat(_permutationNormalTexture->getTexelFormat());
        _permutationNormalTexture->assignStoredMip(0, sizeof(data), data);
    }
    return _permutationNormalTexture;
}

const unsigned char OPAQUE_WHITE[] = { 0xFF, 0xFF, 0xFF, 0xFF };
const unsigned char OPAQUE_GRAY[] = { 0x80, 0x80, 0x80, 0xFF };
const unsigned char OPAQUE_BLUE[] = { 0x80, 0x80, 0xFF, 0xFF };
const unsigned char OPAQUE_BLACK[] = { 0x00, 0x00, 0x00, 0xFF };

const gpu::TexturePointer& TextureCache::getWhiteTexture() {
    if (!_whiteTexture) {
        _whiteTexture = gpu::TexturePointer(gpu::Texture::createStrict(gpu::Element::COLOR_RGBA_32, 1, 1));
        _whiteTexture->setSource("TextureCache::_whiteTexture");
        _whiteTexture->setStoredMipFormat(_whiteTexture->getTexelFormat());
        _whiteTexture->assignStoredMip(0, sizeof(OPAQUE_WHITE), OPAQUE_WHITE);
    }
    return _whiteTexture;
}

const gpu::TexturePointer& TextureCache::getGrayTexture() {
    if (!_grayTexture) {
        _grayTexture = gpu::TexturePointer(gpu::Texture::createStrict(gpu::Element::COLOR_RGBA_32, 1, 1));
        _grayTexture->setSource("TextureCache::_grayTexture");
        _grayTexture->setStoredMipFormat(_grayTexture->getTexelFormat());
        _grayTexture->assignStoredMip(0, sizeof(OPAQUE_GRAY), OPAQUE_GRAY);
    }
    return _grayTexture;
}

const gpu::TexturePointer& TextureCache::getBlueTexture() {
    if (!_blueTexture) {
        _blueTexture = gpu::TexturePointer(gpu::Texture::createStrict(gpu::Element::COLOR_RGBA_32, 1, 1));
        _blueTexture->setSource("TextureCache::_blueTexture");
        _blueTexture->setStoredMipFormat(_blueTexture->getTexelFormat());
        _blueTexture->assignStoredMip(0, sizeof(OPAQUE_BLUE), OPAQUE_BLUE);
    }
    return _blueTexture;
}

const gpu::TexturePointer& TextureCache::getBlackTexture() {
    if (!_blackTexture) {
        _blackTexture = gpu::TexturePointer(gpu::Texture::createStrict(gpu::Element::COLOR_RGBA_32, 1, 1));
        _blackTexture->setSource("TextureCache::_blackTexture");
        _blackTexture->setStoredMipFormat(_blackTexture->getTexelFormat());
        _blackTexture->assignStoredMip(0, sizeof(OPAQUE_BLACK), OPAQUE_BLACK);
    }
    return _blackTexture;
}

/// Extra data for creating textures.
class TextureExtra {
public:
    NetworkTexture::Type type;
    const QByteArray& content;
    int maxNumPixels;
};

ScriptableResource* TextureCache::prefetch(const QUrl& url, int type, int maxNumPixels) {
    auto byteArray = QByteArray();
    TextureExtra extra = { (Type)type, byteArray, maxNumPixels };
    return ResourceCache::prefetch(url, &extra);
}

NetworkTexturePointer TextureCache::getTexture(const QUrl& url, Type type, const QByteArray& content, int maxNumPixels) {
    TextureExtra extra = { type, content, maxNumPixels };
    return ResourceCache::getResource(url, QUrl(), &extra).staticCast<NetworkTexture>();
}

gpu::TexturePointer TextureCache::getTextureByHash(const std::string& hash) {
    std::weak_ptr<gpu::Texture> weakPointer;
    {
        std::unique_lock<std::mutex> lock(_texturesByHashesMutex);
        weakPointer = _texturesByHashes[hash];
    }
    auto result = weakPointer.lock();
    if (result) {
        qCWarning(modelnetworking) << "QQQ Returning live texture for hash " << hash.c_str();
    }
    return result;
}

gpu::TexturePointer TextureCache::cacheTextureByHash(const std::string& hash, const gpu::TexturePointer& texture) {
    gpu::TexturePointer result;
    {
        std::unique_lock<std::mutex> lock(_texturesByHashesMutex);
        result = _texturesByHashes[hash].lock();
        if (!result) {
            _texturesByHashes[hash] = texture;
            result = texture;
        } else {
            qCWarning(modelnetworking) << "QQQ Swapping out texture with previous live texture in hash " << hash.c_str();
        }
    }
    return result;
}


gpu::TexturePointer getFallbackTextureForType(NetworkTexture::Type type) {
    gpu::TexturePointer result;
    auto textureCache = DependencyManager::get<TextureCache>();
    // Since this can be called on a background thread, there's a chance that the cache 
    // will be destroyed by the time we request it
    if (!textureCache) {
        return result;
    }
    switch (type) {
        case NetworkTexture::DEFAULT_TEXTURE:
        case NetworkTexture::ALBEDO_TEXTURE:
        case NetworkTexture::ROUGHNESS_TEXTURE:
        case NetworkTexture::OCCLUSION_TEXTURE:
            result = textureCache->getWhiteTexture();
            break;

        case NetworkTexture::NORMAL_TEXTURE:
            result = textureCache->getBlueTexture();
            break;

        case NetworkTexture::EMISSIVE_TEXTURE:
        case NetworkTexture::LIGHTMAP_TEXTURE:
            result = textureCache->getBlackTexture();
            break;

        case NetworkTexture::BUMP_TEXTURE:
        case NetworkTexture::SPECULAR_TEXTURE:
        case NetworkTexture::GLOSS_TEXTURE:
        case NetworkTexture::CUBE_TEXTURE:
        case NetworkTexture::CUSTOM_TEXTURE:
        case NetworkTexture::STRICT_TEXTURE:
        default:
            break;
    }
    return result;
}


NetworkTexture::TextureLoaderFunc getTextureLoaderForType(NetworkTexture::Type type,
                                                          const QVariantMap& options = QVariantMap()) {
    using Type = NetworkTexture;

    switch (type) {
        case Type::ALBEDO_TEXTURE: {
            return model::TextureUsage::createAlbedoTextureFromImage;
            break;
        }
        case Type::EMISSIVE_TEXTURE: {
            return model::TextureUsage::createEmissiveTextureFromImage;
            break;
        }
        case Type::LIGHTMAP_TEXTURE: {
            return model::TextureUsage::createLightmapTextureFromImage;
            break;
        }
        case Type::CUBE_TEXTURE: {
            if (options.value("generateIrradiance", true).toBool()) {
                return model::TextureUsage::createCubeTextureFromImage;
            } else {
                return model::TextureUsage::createCubeTextureFromImageWithoutIrradiance;
            }
            break;
        }
        case Type::BUMP_TEXTURE: {
            return model::TextureUsage::createNormalTextureFromBumpImage;
            break;
        }
        case Type::NORMAL_TEXTURE: {
            return model::TextureUsage::createNormalTextureFromNormalImage;
            break;
        }
        case Type::ROUGHNESS_TEXTURE: {
            return model::TextureUsage::createRoughnessTextureFromImage;
            break;
        }
        case Type::GLOSS_TEXTURE: {
            return model::TextureUsage::createRoughnessTextureFromGlossImage;
            break;
        }
        case Type::SPECULAR_TEXTURE: {
            return model::TextureUsage::createMetallicTextureFromImage;
            break;
        }
        case Type::STRICT_TEXTURE: {
            return model::TextureUsage::createStrict2DTextureFromImage;
            break;
        }
        case Type::CUSTOM_TEXTURE: {
            Q_ASSERT(false);
            return NetworkTexture::TextureLoaderFunc();
            break;
        }

        case Type::DEFAULT_TEXTURE:
        default: {
            return model::TextureUsage::create2DTextureFromImage;
            break;
        }
    }
}

/// Returns a texture version of an image file
gpu::TexturePointer TextureCache::getImageTexture(const QString& path, Type type, QVariantMap options) {
    QImage image = QImage(path);
    auto loader = getTextureLoaderForType(type, options);
    return gpu::TexturePointer(loader(image, QUrl::fromLocalFile(path).fileName().toStdString()));
}

QSharedPointer<Resource> TextureCache::createResource(const QUrl& url, const QSharedPointer<Resource>& fallback,
    const void* extra) {
    const TextureExtra* textureExtra = static_cast<const TextureExtra*>(extra);
    auto type = textureExtra ? textureExtra->type : Type::DEFAULT_TEXTURE;
    auto content = textureExtra ? textureExtra->content : QByteArray();
    auto maxNumPixels = textureExtra ? textureExtra->maxNumPixels : ABSOLUTE_MAX_TEXTURE_NUM_PIXELS;
    NetworkTexture* texture = new NetworkTexture(url, type, content, maxNumPixels);
    return QSharedPointer<Resource>(texture, &Resource::deleter);
}

NetworkTexture::NetworkTexture(const QUrl& url, Type type, const QByteArray& content, int maxNumPixels) :
    Resource(url),
    _type(type),
    _maxNumPixels(maxNumPixels)
{
    _textureSource = std::make_shared<gpu::TextureSource>();

    if (!url.isValid()) {
        _loaded = true;
    }

    // if we have content, load it after we have our self pointer
    if (!content.isEmpty()) {
        _startedLoading = true;
        QMetaObject::invokeMethod(this, "loadContent", Qt::QueuedConnection, Q_ARG(const QByteArray&, content));
    }
}

NetworkTexture::TextureLoaderFunc NetworkTexture::getTextureLoader() const {
    if (_type == CUSTOM_TEXTURE) {
        return _textureLoader;
    }
    return getTextureLoaderForType(_type);
}

void NetworkTexture::setImage(gpu::TexturePointer texture, int originalWidth,
                              int originalHeight) {
    _originalWidth = originalWidth;
    _originalHeight = originalHeight;

    // Passing ownership
    _textureSource->resetTexture(texture);

    if (texture) {
        _width = texture->getWidth();
        _height = texture->getHeight();
        setSize(texture->getStoredSize());
    } else {
        // FIXME: If !gpuTexture, we failed to load!
        _width = _height = 0;
        qWarning() << "Texture did not load";
    }

    finishedLoading(true);

    emit networkTextureCreated(qWeakPointerCast<NetworkTexture, Resource> (_self));
}

gpu::TexturePointer NetworkTexture::getFallbackTexture() const {
    if (_type == CUSTOM_TEXTURE) {
        return gpu::TexturePointer();
    }
    return getFallbackTextureForType(_type);
}

class Reader : public QRunnable {
public:
    Reader(const QWeakPointer<Resource>& resource, const QUrl& url);
    void run() override final;
    virtual void read() = 0;

protected:
    QWeakPointer<Resource> _resource;
    QUrl _url;
};

class ImageReader : public Reader {
public:
    ImageReader(const QWeakPointer<Resource>& resource, const QUrl& url,
            const QByteArray& data, const std::string& hash, int maxNumPixels);
    void read() override final;

private:
    static void listSupportedImageFormats();

    QByteArray _content;
    std::string _hash;
    int _maxNumPixels;
};

void NetworkTexture::downloadFinished(const QByteArray& data) {
    loadContent(data);
}

void NetworkTexture::loadContent(const QByteArray& content) {
    // Hash the source image to for KTX caching
    std::string hash;
    {
        QCryptographicHash hasher(QCryptographicHash::Md5);
        hasher.addData(content);
        hash = hasher.result().toHex().toStdString();
    }

    auto textureCache = static_cast<TextureCache*>(_cache.data());

    if (textureCache != nullptr) {
        // If we already have a live texture with the same hash, use it
        auto texture = textureCache->getTextureByHash(hash);

        // If there is no live texture, check if there's an existing KTX file
        if (!texture) {
            KTXFilePointer ktxFile = textureCache->_ktxCache.getFile(hash);
            if (ktxFile) {
                // Ensure that the KTX deserialization worked
                auto ktx = ktxFile->getKTX();
                if (ktx) {
                    texture.reset(gpu::Texture::unserialize(ktx));
                    // Ensure that the texture population worked
                    if (texture) {
                        texture->setKtxBacking(ktx);
                        texture = textureCache->cacheTextureByHash(hash, texture);
                    }
                }
            }
        }

        // If we found the texture either because it's in use or via KTX deserialization, 
        // set the image and return immediately.
        if (texture) {
            setImage(texture, texture->getWidth(), texture->getHeight());
            return;
        }
    }

    // We failed to find an existing live or KTX texture, so trigger an image reader
    QThreadPool::globalInstance()->start(new ImageReader(_self, _url, content, hash, _maxNumPixels));
}

Reader::Reader(const QWeakPointer<Resource>& resource, const QUrl& url) :
    _resource(resource), _url(url) {
    DependencyManager::get<StatTracker>()->incrementStat("PendingProcessing");
}

void Reader::run() {
    PROFILE_RANGE_EX(resource_parse_image, __FUNCTION__, 0xffff0000, 0, { { "url", _url.toString() } });
    DependencyManager::get<StatTracker>()->decrementStat("PendingProcessing");
    CounterStat counter("Processing");

    auto originalPriority = QThread::currentThread()->priority();
    if (originalPriority == QThread::InheritPriority) {
        originalPriority = QThread::NormalPriority;
    }
    QThread::currentThread()->setPriority(QThread::LowPriority);
    Finally restorePriority([originalPriority]{ QThread::currentThread()->setPriority(originalPriority); });

    if (!_resource.data()) {
        qCWarning(modelnetworking) << "Abandoning load of" << _url << "; could not get strong ref";
        return;
    }

    read();
}

ImageReader::ImageReader(const QWeakPointer<Resource>& resource, const QUrl& url,
        const QByteArray& data, const std::string& hash, int maxNumPixels) :
    Reader(resource, url), _content(data), _hash(hash), _maxNumPixels(maxNumPixels) {
    listSupportedImageFormats();

#if DEBUG_DUMP_TEXTURE_LOADS
    static auto start = usecTimestampNow() / USECS_PER_MSEC;
    auto now = usecTimestampNow() / USECS_PER_MSEC - start;
    QString urlStr = _url.toString();
    auto dot = urlStr.lastIndexOf(".");
    QString outFileName = QString(QCryptographicHash::hash(urlStr.toLocal8Bit(), QCryptographicHash::Md5).toHex()) + urlStr.right(urlStr.length() - dot);
    QFile loadRecord("h:/textures/loads.txt");
    loadRecord.open(QFile::Text | QFile::Append | QFile::ReadWrite);
    loadRecord.write(QString("%1 %2\n").arg(now).arg(outFileName).toLocal8Bit());
    outFileName = "h:/textures/" + outFileName;
    QFileInfo outInfo(outFileName);
    if (!outInfo.exists()) {
        QFile outFile(outFileName);
        outFile.open(QFile::WriteOnly | QFile::Truncate);
        outFile.write(data);
        outFile.close();
    }
#endif
}

void ImageReader::listSupportedImageFormats() {
    static std::once_flag once;
    std::call_once(once, []{
        auto supportedFormats = QImageReader::supportedImageFormats();
        qCDebug(modelnetworking) << "List of supported Image formats:" << supportedFormats.join(", ");
    });
}

void ImageReader::read() {
    // Help the QImage loader by extracting the image file format from the url filename ext.
    // Some tga are not created properly without it.
    auto filename = _url.fileName().toStdString();
    auto filenameExtension = filename.substr(filename.find_last_of('.') + 1);
    QImage image = QImage::fromData(_content, filenameExtension.c_str());
    int imageWidth = image.width();
    int imageHeight = image.height();

    // Validate that the image loaded
    if (imageWidth == 0 || imageHeight == 0 || image.format() == QImage::Format_Invalid) {
        QString reason(filenameExtension.empty() ? "" : "(no file extension)");
        qCWarning(modelnetworking) << "Failed to load" << _url << reason;
        return;
    }

    // Validate the image is less than _maxNumPixels, and downscale if necessary
    if (imageWidth * imageHeight > _maxNumPixels) {
        float scaleFactor = sqrtf(_maxNumPixels / (float)(imageWidth * imageHeight));
        int originalWidth = imageWidth;
        int originalHeight = imageHeight;
        imageWidth = (int)(scaleFactor * (float)imageWidth + 0.5f);
        imageHeight = (int)(scaleFactor * (float)imageHeight + 0.5f);
        QImage newImage = image.scaled(QSize(imageWidth, imageHeight), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        image.swap(newImage);
        qCDebug(modelnetworking).nospace() << "Downscaled " << _url << " (" <<
            QSize(originalWidth, originalHeight) << " to " <<
            QSize(imageWidth, imageHeight) << ")";
    }

    gpu::TexturePointer texture = nullptr;
    {
        auto resource = _resource.lock(); // to ensure the resource is still needed
        if (!resource) {
            qCDebug(modelnetworking) << _url << "loading stopped; resource out of scope";
            return;
        }

        auto url = _url.toString().toStdString();

        PROFILE_RANGE_EX(resource_parse_image_raw, __FUNCTION__, 0xffff0000, 0);
        // Load the image into a gpu::Texture
        auto networkTexture = resource.staticCast<NetworkTexture>();
        texture.reset(networkTexture->getTextureLoader()(image, url));
        texture->setSource(url);
        if (texture) {
            texture->setFallbackTexture(networkTexture->getFallbackTexture());
        }

        auto textureCache = DependencyManager::get<TextureCache>();
        // Save the image into a KTXFile
        auto memKtx = gpu::Texture::serialize(*texture);
        if (!memKtx) {
            qCWarning(modelnetworking) << "Unable to serialize texture to KTX " << _url;
        }

        if (memKtx && textureCache) {
            const char* data = reinterpret_cast<const char*>(memKtx->_storage->data());
            size_t length = memKtx->_storage->size();
            KTXFilePointer file;
            auto& ktxCache = textureCache->_ktxCache;
            if (!memKtx || !(file = ktxCache.writeFile(data, KTXCache::Metadata(_hash, length)))) {
                qCWarning(modelnetworking) << _url << "file cache failed";
            } else {
                resource.staticCast<NetworkTexture>()->_file = file;
                auto fileKtx = file->getKTX();
                if (fileKtx) {
                    texture->setKtxBacking(fileKtx);
                }
            }
        }

        // We replace the texture with the one stored in the cache.  This deals with the possible race condition of two different 
        // images with the same hash being loaded concurrently.  Only one of them will make it into the cache by hash first and will
        // be the winner
        if (textureCache) {
            texture = textureCache->cacheTextureByHash(_hash, texture);
        }
    }

    auto resource = _resource.lock(); // to ensure the resource is still needed
    if (resource) {
        QMetaObject::invokeMethod(resource.data(), "setImage",
            Q_ARG(gpu::TexturePointer, texture),
            Q_ARG(int, imageWidth), Q_ARG(int, imageHeight));
    } else {
        qCDebug(modelnetworking) << _url << "loading stopped; resource out of scope";
    }
}
