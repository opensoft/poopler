/*
 * Copyright (C) 2010, Pino Toscano <pino@kde.org>
 * Copyright (C) 2015 William Bader <williambader@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "poppler-page-renderer.h"

#include "poppler-document-private.h"
#include "poppler-page-private.h"

#include <config.h>

#include "PDFDoc.h"
#if defined(HAVE_SPLASH)
#include "SplashOutputDev.h"
#include "splash/SplashBitmap.h"
#endif

#include <memory>
#include <map>
#include <list>
#include <array>
class SplashOutputDevice : public SplashOutputDev
{
    bool transparent = true;
    struct Color
    {
        enum Format
        {
            DeviceGray = csDeviceGray,
            DeviceRGB = csDeviceRGB,
            DeviceCMYK = csDeviceCMYK,

            CalGray = csCalGray,
            CalRGB = csCalRGB,
            Lab = csLab,
            ICCBased = csICCBased,

            Indexed = csIndexed,
            Separation = csSeparation,
            DeviceN = csDeviceN,
            Pattern = csPattern
        } format;

        std::vector<float> colors;
    };

    struct ImageData
    {
        ImageData(Stream *str, int _width, int _height, GfxImageColorMap *colorMap, GBool inlineImg = gFalse)
            : width(_width), height(_height)
        {
            bitsPerChannel = colorMap->getBits();
            channels = colorMap->getNumPixelComps();
            rowSize = (bitsPerChannel * channels * width + 7) / 8;
            format = static_cast<Color::Format>(colorMap->getColorSpace()->getMode());

            download(str, inlineImg);
        }

        ImageData(Stream *str, int _width, int _height, GBool inlineImg = gFalse)
            : width(_width), height(_height)
        {
            bitsPerChannel = 1;
            channels = 1;
            rowSize = (bitsPerChannel * channels * width + 7) / 8;
            format = Color::DeviceGray;

            download(str, inlineImg);
        }
        void download(Stream *str, GBool inlineImg)
        {
            data.resize(rowSize * height);
            str->reset();
            int readChars = str->doGetChars(data.size(), data.data());
            str->close();
        }
        Color::Format format;
        int bitsPerChannel;
        int channels;
        int width;
        int height;
        int rowSize;
        //Each n-bit unit within the bit stream is interpreted as an unsigned integer
        //in the range 0 to 2n−1, with the high-order bit first
        // So, PDF use big-endian bit order. It is awfull, because
        // in mask case 128 - set first pixel to 1 and 1 - set eighth pixel (less bit set high pixel)
        // I hope that after decoding byte-order is set to right order.
        std::vector<unsigned char> data;
    };
    using TransformationMatrix = std::array<double, 6>;
    struct ImageDraw
    {
        std::shared_ptr<ImageData> image;
        std::shared_ptr<ImageData> mask;
        //1. if mask empty and maskColors is not - then it contain min and max pairs for each
        // color component of pixel which must be excluded form draw (chromokey?) 
        //2. if mask isn't empty maskColors used for set inversion of mask: if maskColors[0] > maskColors[1]
        std::vector<int> maskColors;
        // When image is empty this color used for drawing mask.
        // We can't use image in this case because 1 (0 if inverse) in pixel is transparent - not drawing
        Color fillColor;
        void setFillColor(int colorFormat, int channels, int *colors)
        {
            fillColor.format = static_cast<Color::Format>(colorFormat);
            for (int i = 0; i < channels; i++)
                fillColor.colors.push_back(colors[i]);
        }

        TransformationMatrix transform;
        void setTransformationMatrix(const double * ctm)
        {
            std::copy(ctm, ctm + 6, transform.begin());
        }

        void setMaskInversion(bool invert)
        {
            if (invert)
                maskColors = {1, 0};
            else
                maskColors = {0, 1};
        }


    };

    using SharedImageData = std::shared_ptr<ImageData>;
    using PdfReference = std::pair<int, int>;
    using ImageStore = std::map<PdfReference, SharedImageData>;
    ImageStore m_images;
    ImageStore m_imageMasks;
    std::list<SharedImageData> m_inlineImages;

    std::list<ImageDraw> drawLaiers;

    template<typename ... Args>
    SharedImageData sharedImage(ImageStore &imageStore, Object *ref, Args && ...args)
    {
        PdfReference reference;
        if (ref) {
            reference = PdfReference(ref->getRef().num, ref->getRef().gen);
            auto imageIterator = imageStore.find(reference);
            if (imageIterator != imageStore.end() && imageIterator->second)
                return imageIterator->second;
        }
        auto sharedImageData = std::make_shared<ImageData>(args...);
        if (ref)
            imageStore[reference] = sharedImageData;
        else
            m_inlineImages.push_back(sharedImageData);

        return sharedImageData;
    }
    template<typename ... Args>
    SharedImageData sharedImage(Args && ...args)
    {
        return sharedImage(m_images, args...);
    }

    template<typename ... Args>
    SharedImageData sharedMask(Args && ...args)
    {
        return sharedImage(m_imageMasks, args...);
    }
public:
    SplashOutputDevice(SplashColorMode colorModeA, int bitmapRowPadA,
                       GBool reverseVideoA, SplashColorPtr paperColorA,
                       GBool bitmapTopDownA = gTrue,
                       SplashThinLineMode thinLineMode = splashThinLineDefault,
                       GBool overprintPreviewA = globalParams->getOverprintPreview())
        :SplashOutputDev(colorModeA, bitmapRowPadA, reverseVideoA, paperColorA,
                         bitmapTopDownA, thinLineMode, overprintPreviewA)
    {
    }

    void drawChar(GfxState *state, double x, double y,
                  double dx, double dy,
                  double originX, double originY,
                  CharCode code, int nBytes,
                  Unicode *u, int uLen) override
    {
        if (transparent)
            SplashOutputDev::drawChar(state, x, y, dx, dy, originX, originY, code, nBytes, u, uLen);
    }
    //----- image drawing
    void drawImageMask(GfxState *state, Object *ref, Stream *str,
                       int width, int height, GBool invert,
                       GBool interpolate, GBool inlineImg) override
    {
        ImageDraw imageDraw;
        imageDraw.mask = sharedMask(ref, str, width, height, inlineImg);
        imageDraw.setFillColor(state->getFillColorSpace()->getMode(), state->getFillColorSpace()->getNComps(), state->getFillColor()->c);
        imageDraw.setMaskInversion(invert);
        imageDraw.setTransformationMatrix(state->getCTM());
        drawLaiers.push_back(imageDraw);

        if (transparent && !inlineImg)
            SplashOutputDev::drawImageMask(state, ref, str, width, height, invert, interpolate, inlineImg);
    }

    void drawImage(GfxState *state, Object *ref, Stream *str,
                   int width, int height, GfxImageColorMap *colorMap,
                   GBool interpolate, int *maskColors, GBool inlineImg) override
    {
        ImageDraw imageDraw;
        imageDraw.image = sharedImage(ref, str, width, height, colorMap, inlineImg);
        if (maskColors)
            imageDraw.maskColors.assign(maskColors, maskColors + 2 * imageDraw.image->channels);

        imageDraw.setTransformationMatrix(state->getCTM());
        drawLaiers.push_back(imageDraw);

        if (transparent && !inlineImg)
            SplashOutputDev::drawImage(state, ref, str, width, height, colorMap, interpolate, maskColors, inlineImg);
    }

    void drawMaskedImage(GfxState *state, Object *ref, Stream *str,
                         int width, int height,
                         GfxImageColorMap *colorMap,
                         GBool interpolate,
                         Stream *maskStr, int maskWidth, int maskHeight,
                         GBool maskInvert, GBool maskInterpolate) override
    {
        ImageDraw imageDraw;
        imageDraw.image = sharedImage(ref, str, width, height, colorMap);
        imageDraw.mask = sharedMask(ref, maskStr, maskWidth, maskHeight);
        imageDraw.setMaskInversion(maskInvert);
        imageDraw.setTransformationMatrix(state->getCTM());
        drawLaiers.push_back(imageDraw);

        if (transparent)
            SplashOutputDev::drawMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskInvert, maskInterpolate);
    }

    void drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str,
                             int width, int height,
                             GfxImageColorMap *colorMap,
                             GBool interpolate,
                             Stream *maskStr,
                             int maskWidth, int maskHeight,
                             GfxImageColorMap *maskColorMap,
                             GBool maskInterpolate) override
    {
        ImageDraw imageDraw;
        imageDraw.image = sharedImage(ref, str, width, height, colorMap);
        imageDraw.mask = sharedMask(ref, maskStr, maskWidth, maskHeight, maskColorMap);

        imageDraw.setTransformationMatrix(state->getCTM());
        drawLaiers.push_back(imageDraw);
        if (transparent)
            SplashOutputDev::drawSoftMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskColorMap, maskInterpolate);
    }
    //----- path painting
    void stroke(GfxState *state)
    {
        if (transparent)
            SplashOutputDev::stroke(state);
    }

    void fill(GfxState *state)
    {
        if (transparent)
            SplashOutputDev::fill(state);
    }

    void eoFill(GfxState *state)
    {
        if (transparent)
            SplashOutputDev::eoFill(state);
    }

protected:
private:
};
using namespace poppler;

class poppler::page_renderer_private
{
public:
    page_renderer_private()
        : paper_color(0xffffffff)
        , hints(0)
    {
    }

    argb paper_color;
    unsigned int hints;
};


/**
 \class poppler::page_renderer poppler-page-renderer.h "poppler/cpp/poppler-renderer.h"

 Simple way to render a page of a PDF %document.

 \since 0.16
 */

/**
 \enum poppler::page_renderer::render_hint

 A flag of an option taken into account when rendering
*/


/**
 Constructs a new %page renderer.
 */
page_renderer::page_renderer()
    : d(new page_renderer_private())
{
}

/**
 Destructor.
 */
page_renderer::~page_renderer()
{
    delete d;
}

/**
 The color used for the "paper" of the pages.

 The default color is opaque solid white (0xffffffff).

 \returns the paper color
 */
argb page_renderer::paper_color() const
{
    return d->paper_color;
}

/**
 Set a new color for the "paper".

 \param c the new color
 */
void page_renderer::set_paper_color(argb c)
{
    d->paper_color = c;
}

/**
 The hints used when rendering.

 By default no hint is set.

 \returns the render hints set
 */
unsigned int page_renderer::render_hints() const
{
    return d->hints;
}

/**
 Enable or disable a single render %hint.

 \param hint the hint to modify
 \param on whether enable it or not
 */
void page_renderer::set_render_hint(page_renderer::render_hint hint, bool on)
{
    if (on) {
        d->hints |= hint;
    } else {
        d->hints &= ~(int)hint;
    }
}

/**
 Set new render %hints at once.

 \param hints the new set of render hints
 */
void page_renderer::set_render_hints(unsigned int hints)
{
    d->hints = hints;
}

/**
 Render the specified page.

 This functions renders the specified page on an image following the specified
 parameters, returning it.

 \param p the page to render
 \param xres the X resolution, in dot per inch (DPI)
 \param yres the Y resolution, in dot per inch (DPI)
 \param x the X top-right coordinate, in pixels
 \param y the Y top-right coordinate, in pixels
 \param w the width in pixels of the area to render
 \param h the height in pixels of the area to render
 \param rotate the rotation to apply when rendering the page

 \returns the rendered image, or a null one in case of errors

 \see can_render
 */
image page_renderer::render_page(const page *p,
                                 double xres, double yres,
                                 int x, int y, int w, int h,
                                 rotation_enum rotate) const
{
    if (!p) {
        return image();
    }

#if defined(HAVE_SPLASH)
    page_private *pp = page_private::get(p);
    PDFDoc *pdfdoc = pp->doc->doc;

    SplashColor bgColor;
    bgColor[0] = d->paper_color & 0xff;
    bgColor[1] = (d->paper_color >> 8) & 0xff;
    bgColor[2] = (d->paper_color >> 16) & 0xff;
    SplashOutputDevice splashOutputDev(splashModeXBGR8, 4, gFalse, bgColor, gTrue);
    splashOutputDev.setFontAntialias(d->hints & text_antialiasing ? gTrue : gFalse);
    splashOutputDev.setVectorAntialias(d->hints & antialiasing ? gTrue : gFalse);
    splashOutputDev.setFreeTypeHinting(d->hints & text_hinting ? gTrue : gFalse, gFalse);
    splashOutputDev.startDoc(pdfdoc);
    pdfdoc->displayPageSlice(&splashOutputDev, pp->index + 1,
                             xres, yres, int(rotate) * 90,
                             gFalse, gTrue, gFalse,
                             x, y, w, h);

    SplashBitmap *bitmap = splashOutputDev.getBitmap();
    const int bw = bitmap->getWidth();
    const int bh = bitmap->getHeight();

    SplashColorPtr data_ptr = bitmap->getDataPtr();

    const image img(reinterpret_cast<char *>(data_ptr), bw, bh, image::format_argb32);
    return img.copy();
#else
    return image();
#endif
}

/**
 Rendering capability test.

 page_renderer can render only if a render backend ('Splash') is compiled in
 Poppler.

 \returns whether page_renderer can render
 */
bool page_renderer::can_render()
{
#if defined(HAVE_SPLASH)
    return true;
#else
    return false;
#endif
}
