/*
 * Copyright (C) 2010, Pino Toscano <pino@kde.org>
 * Copyright (C) 2015 William Bader <williambader@hotmail.com>
 * Copyright (C) 2018, Zsombor Hollay-Horvath <hollay.horvath@gmail.com>
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
#include "poppler-image.h"
#include "ExternDrawOutputDivice.h"

#include <config.h>

#include "PDFDoc.h"
#if defined(HAVE_SPLASH)
#include "SplashOutputDev.h"
#include "splash/SplashBitmap.h"
#endif

using namespace poppler;

class poppler::page_renderer_private
{
public:
    page_renderer_private()
        : paper_color(0xffffffff)
        , hints(0)
        , image_format(image::format_enum::format_argb32)
        , line_mode(page_renderer::line_mode_enum::line_default)
    {
    }

#if defined(HAVE_SPLASH)
    static bool conv_color_mode(image::format_enum mode,
                                SplashColorMode &splash_mode);
    static bool conv_line_mode(page_renderer::line_mode_enum mode,
                               SplashThinLineMode &splash_mode);
#endif

    argb paper_color;
    unsigned int hints;
    image::format_enum image_format;
    page_renderer::line_mode_enum line_mode;

};


#if defined(HAVE_SPLASH)
bool page_renderer_private::conv_color_mode(image::format_enum mode,
                                            SplashColorMode &splash_mode)
{
    switch (mode) {
        case image::format_enum::format_mono:
            splash_mode = splashModeMono1;
            break;
        case image::format_enum::format_gray8:
            splash_mode = splashModeMono8;
            break;
        case image::format_enum::format_rgb24:
            splash_mode = splashModeRGB8;
            break;
        case image::format_enum::format_bgr24:
            splash_mode = splashModeBGR8;
            break;
        case image::format_enum::format_argb32:
            splash_mode = splashModeXBGR8;
            break;
        default:
            return false;
    }
    return true;
}

bool page_renderer_private::conv_line_mode(page_renderer::line_mode_enum mode,
                                           SplashThinLineMode &splash_mode)
{
    switch (mode) {
        case page_renderer::line_mode_enum::line_default:
            splash_mode = splashThinLineDefault;
            break;
        case page_renderer::line_mode_enum::line_solid:
            splash_mode = splashThinLineSolid;
            break;
        case page_renderer::line_mode_enum::line_shape:
            splash_mode = splashThinLineShape;
            break;
        default:
            return false;
    }
    return true;
}
#endif

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
 The image format used when rendering.

 By default ARGB32 is set.

 \returns the image format

 \since 0.65
 */
image::format_enum page_renderer::image_format() const
{
    return d->image_format;
}

/**
 Set new image format used when rendering.

 \param format the new image format

 \since 0.65
 */
void page_renderer::set_image_format(image::format_enum format)
{
    d->image_format = format;
}

/**
 The line mode used when rendering.

 By default default mode is set.

 \returns the line mode

 \since 0.65
 */
page_renderer::line_mode_enum page_renderer::line_mode() const
{
    return d->line_mode;
}

/**
 Set new line mode used when rendering.

 \param mode the new line mode

 \since 0.65
 */
void page_renderer::set_line_mode(page_renderer::line_mode_enum mode)
{
    d->line_mode = mode;
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

    SplashColorMode colorMode;
    SplashThinLineMode lineMode;

    if (!d->conv_color_mode(d->image_format, colorMode) ||
        !d->conv_line_mode(d->line_mode, lineMode)) {
        return image();
    }

    SplashColor bgColor;
    bgColor[0] = d->paper_color & 0xff;
    bgColor[1] = (d->paper_color >> 8) & 0xff;
    bgColor[2] = (d->paper_color >> 16) & 0xff;
    SplashOutputDev splashOutputDev(colorMode, 4, false, bgColor, true, lineMode);
    splashOutputDev.setFontAntialias(d->hints & text_antialiasing ? true : false);
    splashOutputDev.setVectorAntialias(d->hints & antialiasing ? true : false);
    splashOutputDev.setFreeTypeHinting(d->hints & text_hinting ? true : false, false);
    splashOutputDev.startDoc(pdfdoc);
    pdfdoc->displayPageSlice(&splashOutputDev, pp->index + 1,
                             xres, yres, int(rotate) * 90,
                             false, true, false,
                             x, y, w, h);

    SplashBitmap *bitmap = splashOutputDev.getBitmap();
    const int bw = bitmap->getWidth();
    const int bh = bitmap->getHeight();

    SplashColorPtr data_ptr = bitmap->getDataPtr();

    const image img(reinterpret_cast<char *>(data_ptr), bw, bh, d->image_format);
    return img.copy();
#else
    return image();
#endif
}

struct SplashBitmapResultPrivate : public page_renderer::SplashBitmapResult
{
    SplashBitmapResultPrivate(/*const*/ SplashBitmap &splashBitmap)
    {
        imageData.width = splashBitmap.getWidth();
        imageData.height = splashBitmap.getHeight();
        imageData.rowSize = splashBitmap.getRowSize();
        const auto data = reinterpret_cast<unsigned char*>(splashBitmap.getDataPtr());
        try {
            imageData.data.assign(data, data + imageData.rowSize * imageData.height);
        } catch (const std::exception &) {
            imageData.data.clear();
        }
        imageData.format = Color::CalGray;
        imageData.bitsPerChannel = 8;
        imageData.channels = 1;
        switch (splashBitmap.getMode()) {
        case splashModeMono1:
            imageData.bitsPerChannel = 1;
        case splashModeMono8:
            break;
        case splashModeRGB8:
        case splashModeBGR8:
            imageData.format = Color::DeviceRGB;
            imageData.channels = 3;
            break;
        case splashModeXBGR8:
            imageData.format = Color::DeviceRGB;
            imageData.channels = 4;
            break;
#if SPLASH_CMYK
        case splashModeCMYK8:
            imageData.format = Color::DeviceCMYK;
            imageData.channels = 4;
            break;
        case splashModeDeviceN8:
            imageData.format = Color::DeviceN;
            imageData.channels = 8;
            break;
#endif
        default:
            break;
        }
    }
};

ProcessStepStore page_renderer::extract_process(const page *p, double xres /*= 72.0*/, double yres /*= 72.0*/,
                                                splash_color_mode splashMode /*= splash_color_mode::NotSet*/,
                                                bool withImageData) const
{
    if (!p) {
        return ProcessStepStore();
    }

#if defined(HAVE_SPLASH)
    page_private *pp = page_private::get(p);
    PDFDoc *pdfdoc = pp->doc->doc;

    SplashColor bgColor;
    bgColor[0] = d->paper_color & 0xff;
    bgColor[1] = (d->paper_color >> 8) & 0xff;
    bgColor[2] = (d->paper_color >> 16) & 0xff;
    bool useSplashDraw = splashMode != splash_color_mode::NotSet;
    SplashColorMode splashColorMode = useSplashDraw ? static_cast<SplashColorMode>(splashMode) : splashModeXBGR8;

    ExternDrawOutputDivice splashOutputDev(useSplashDraw, withImageData, splashColorMode, 1, false, bgColor, true);
    splashOutputDev.setFontAntialias(d->hints & text_antialiasing ? true : false);
    splashOutputDev.setVectorAntialias(d->hints & antialiasing ? true : false);
    splashOutputDev.setFreeTypeHinting(d->hints & text_hinting ? true : false, false);
    splashOutputDev.startDoc(pdfdoc);
    pdfdoc->displayPageSlice(&splashOutputDev, pp->index + 1,
                             xres, yres, 0,
                             false, true, false,
                             -1, -1, -1, -1);
    auto drawingSteps = splashOutputDev.drawingSteps();
    if(useSplashDraw)
        drawingSteps.push_back(std::make_shared<SplashBitmapResultPrivate>(*splashOutputDev.getBitmap()));

    return drawingSteps;
#else
    return ProcessStepStore();
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
