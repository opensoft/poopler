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
#include "GfxFont.h"
#if defined(HAVE_SPLASH)
#include "SplashOutputDev.h"
#include "splash/Splash.h"
#include "splash/SplashBitmap.h"
#include "splash/SplashFont.h"
#include "splash/SplashFontFile.h"
#include "splash/SplashPath.h"
#endif

#include <memory>
#include <map>
#include <list>
#include <array>
 // [ m[0] m[1] 0 ]
 // [ m[2] m[3] 0 ]
 // [ m[4] m[5] 1 ]
using TransformationMatrix = std::array<double, 6>;
// x = m[0]  y = m[1]. Point in PDF is one row vector (matrix 1x3) [x y 1]
using Point = std::array<double, 2>;

TransformationMatrix &operator<<(TransformationMatrix &t, const double * ctm)
{
    std::copy(ctm, ctm + 6, t.begin());
    return t;
}
// Transform a point from user space to device space. Multiplication point on matrix
Point operator*(const Point &p, const TransformationMatrix &matrix)
{
    return {
        p[0] * matrix[0] + p[1] * matrix[2] + matrix[4],
        p[0] * matrix[1] + p[1] * matrix[3] + matrix[5]
    };
}
//result = a * b;
TransformationMatrix operator*(const TransformationMatrix &a, const TransformationMatrix &b)
{
    return {
        a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
        a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
        a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]
    };
}
//like multiplication matrix on shift matrix:
//          [1 0 0]
// matrix * [0 1 0]
//          [x y 1]
TransformationMatrix operator+(const TransformationMatrix &matrix, const Point &p)
{
    return{
        matrix[0], matrix[1],
        matrix[2], matrix[3],
        matrix[4] + p[0], matrix[5] + p[1],
    };
}

//like multiplication shift matrix on matrix:
//[1 0 0]          
//[0 1 0] * matrix
//[x y 1]          
TransformationMatrix operator+(const Point &p, const TransformationMatrix &m)
{
    return{
        m[0], m[1],
        m[2], m[3],
        m[4] + p[0] * m[0] + p[1] * m[2], m[5] + p[0] * m[1] + p[1] * m[3],
    };
}
class SplashOutputDevice : public SplashOutputDev
{
    bool transparent = true;
    struct Color
    {
        enum Format
        {
            //Device Color Spaces
            DeviceGray = csDeviceGray,
            DeviceRGB = csDeviceRGB,
            DeviceCMYK = csDeviceCMYK,
            //CIE-Based Color Spaces
            CalGray = csCalGray,
            CalRGB = csCalRGB,
            Lab = csLab,
            ICCBased = csICCBased,

            Indexed = csIndexed,
            Separation = csSeparation,
            DeviceN = csDeviceN,
            Pattern = csPattern
        } format;

        std::vector<float> components;
        void setColor(GfxColorSpace * colorSpace, int *colors16_16)
        {
            if (colorSpace->isNonMarking())
                return;

            format = static_cast<Color::Format>(colorSpace->getMode());
            for (int i = 0; i < colorSpace->getNComps(); i++)
                components.push_back(colors16_16[i]);
        }
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
    struct ContourData
    {
        struct ContourPoint
        {
            double x, y;
            // first point on each subpath sets this flag
            unsigned char firstPoint : 1;// = 0x01,
                // last point on each subpath sets this flag
            unsigned char lastPoint : 1;// = 0x02,
            // if the subpath is closed, its first and last points must be
            // identical, and must set this flag
            unsigned char pathClosed : 1;// = 0x04,
            // curve control points set this flag
            unsigned char pathCurve : 1;// = 0x08,
        };
        ContourData(/*const*/ SplashPath &path)
        {
            points.reserve(path.getLength());
            for (int i = 0; i < path.getLength(); ++i) {
                ContourPoint point;
                Guchar flags;
                path.getPoint(i, &point.x, &point.y, &flags);
                point.firstPoint = flags & splashPathFirst;
                point.lastPoint = flags & splashPathLast;
                point.pathClosed = flags & splashPathClosed;
                point.pathCurve = flags & splashPathCurve;
                points.push_back(point);
            }
        }
        ContourData(/*const*/ GfxPath &path)
        {
            for (int i = 0; i < path.getNumSubpaths(); ++i) {
                int curvePoints = 0;
                std::vector<ContourPoint> contour;
                GfxSubpath *subpath = path.getSubpath(i);
                contour.reserve(subpath->getNumPoints());
                for (int j = 0; j < subpath->getNumPoints(); ++j, --curvePoints) {
                    if (subpath->getCurve(j))
                        curvePoints = 2;

                    ContourPoint point;
                    point.x = subpath->getX(j);
                    point.y = subpath->getY(j);
                    if (curvePoints > 0)
                        point.pathCurve = true;

                    contour.push_back(point);
                }
                contour.front().firstPoint = true;
                contour.back().lastPoint = true;
                if (subpath->isClosed())
                    contour.back().pathClosed = true;

                points.insert(points.end(), contour.begin(), contour.end());
            }
        }
        std::vector<ContourPoint> points;
    };
    using SharedContourData = std::shared_ptr<ContourData>;
    class ProcessStep
    {
    public:
        enum StepInformation
        {
            ContourDraw,
            ImageDraw,
            StateSave,
            StateRestore,
            TextStart,
            TextStop
        };
        virtual StepInformation stepInformation() = 0;
    };
    struct StateSave : public ProcessStep
    {
        StepInformation stepInformation() override
        {
            return DrawStep::StateSave;
        }
    };
    struct StateRestore : public ProcessStep
    {
        StepInformation stepInformation() override
        {
            return DrawStep::StateRestore;
        }
    };
    struct TextStart : public ProcessStep
    {
        StepInformation stepInformation() override
        {
            return DrawStep::TextStart;
        }
    };
    struct TextStop : public ProcessStep
    {
        StepInformation stepInformation() override
        {
            return DrawStep::TextStop;
        }
    };

    struct DrawStep : public ProcessStep
    {
        TransformationMatrix transform;
    };
    struct ContourDraw : public DrawStep
    {
        StepInformation stepInformation() override
        {
            return DrawStep::ContourDraw;
        }
        //affects only if set
        Color fillColor;
        Color strokeColor;
        double lineWidth;
        //if this contour should to be accumulate to current clip state
        // splash clip deleted after StateRestore. Is it right? Could clip 
        // accumulation continue on same level of the state?
        bool clip;
        SharedContourData contourData;
    };
    //struct FontState : public DrawStep
    //{
    //    StepInformation stepInformation() override
    //    {
    //        return DrawStep::FontState;
    //    }
    //    TransformationMatrix textTransform;
    //    TransformationMatrix splashTextTransform;
    //};
    struct ImageDraw : public DrawStep
    {
        StepInformation stepInformation() override
        {
            return DrawStep::ImageDraw;
        }
        std::shared_ptr<ImageData> image;
        std::shared_ptr<ImageData> mask;
        //1. if mask empty and maskColors is not - then it contain min and max pairs for each
        // color component of pixel which must be excluded form draw (chromokey?) 
        //2. if mask isn't empty maskColors used for set inversion of mask: if maskColors[0] > maskColors[1]
        std::vector<int> maskColors;
        // When image is empty this color used for drawing mask.
        // We can't use image (container) in this case because
        // mask 1 (0 if inverse) in pixel is transparent - not drawing
        Color fillColor;

        void setMaskInversion(bool invert)
        {
            if (invert)
                maskColors = {1, 0};
            else
                maskColors = {0, 1};
        }
    };

    using PdfReference = std::pair<int, int>;
    using PdfCharReference = std::pair<PdfReference, CharCode>;
    using SharedImageData = std::shared_ptr<ImageData>;

    using ImageStore = std::map<PdfReference, SharedImageData>;
    using ContourStore = std::map<PdfCharReference, SharedContourData>;
    ImageStore m_images;
    ImageStore m_imageMasks;
    ContourStore m_contours;
    //std::list<SharedImageData> m_inlineImages;

    std::list<std::shared_ptr<ProcessStep>> drawLaiers;

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
        //else
        //    m_inlineImages.push_back(sharedImageData);

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
    //same as parent needFontUpdate which is not accessible 
    bool fontChanged = true;
    SharedContourData sharedContour(/*const*/ GfxState *state, const CharCode &code)
    {
        if (fontChanged) {
            doUpdateFont(state);
            fontChanged = false;
            // This is a hack because splash font make glyph contours
            // using CTM and textCTM instead making it with identity matrix.
            // That is why splash glyphs with same code and font are not identical.
            m_contours.clear();
        }
        auto fontReference = state->getFont()->getID();
        auto reference = PdfCharReference(PdfReference(fontReference->num, fontReference->gen), code);
        auto iterator = m_contours.find(reference);
        if (iterator != m_contours.end() && iterator->second)
            return iterator->second;

        return m_contours[reference] = 
            std::make_shared<ContourData>(*getCurrentFont()->getGlyphPath(code));
    }
    SharedContourData sharedContour(/*const*/ GfxState *state, /*const*/ GfxPath &path)
    {
        return std::make_shared<ContourData>(path);
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
    ////----- get info about output device
    //// Does this device use tilingPatternFill()?  If this returns false,
    //// tiling pattern fills will be reduced to a series of other drawing
    //// operations.
    //GBool useTilingPatternFill() override
    //{
    //    return gTrue;
    //}
    //// Does this device use functionShadedFill(), axialShadedFill(), and
    //// radialShadedFill()?  If this returns false, these shaded fills
    //// will be reduced to a series of other drawing operations.
    //GBool useShadedFills(int type) override
    //{
    //    return (type >= 1 && type <= 5) ? gTrue : gFalse;
    //}
    //// Does this device use upside-down coordinates?
    //// (Upside-down means (0,0) is the top left corner of the page.)
    //GBool upsideDown() override
    //{
    //    return bitmapTopDown ^ bitmapUpsideDown;
    //}
    //// Does this device use drawChar() or drawString()?
    //GBool useDrawChar() override
    //{
    //    return gTrue;
    //}
    //// Does this device use beginType3Char/endType3Char?  Otherwise,
    //// text in Type 3 fonts will be drawn with drawChar/drawString.
    //GBool interpretType3Chars() override
    //{
    //    return gTrue;
    //}
    //----- initialization and control
    // Start a page.
    //void startPage(int pageNum, GfxState *state, XRef *xref) override;
    // End a page.
    //void endPage() override;
    //----- save/restore graphics state
    void saveState(GfxState *state) override
    {
        drawLaiers.push_back(std::make_shared<StateSave>());
        if (transparent)
            SplashOutputDev::saveState(state);
    }
    void restoreState(GfxState *state) override
    {
        drawLaiers.push_back(std::make_shared<StateRestore>());
        fontChanged = true;
        if (transparent)
            SplashOutputDev::restoreState(state);
    }
    //----- update graphics state
    void updateAll(GfxState *state) override
    {
        fontChanged = true;
        if (transparent)
            SplashOutputDev::updateAll(state);
    }
    //void updateCTM(GfxState *state, double m11, double m12,
    //               double m21, double m22, double m31, double m32) override;
    //void updateLineDash(GfxState *state) override;
    //void updateFlatness(GfxState *state) override;
    //void updateLineJoin(GfxState *state) override;
    //void updateLineCap(GfxState *state) override;
    //void updateMiterLimit(GfxState *state) override;
    //void updateLineWidth(GfxState *state) override;
    //void updateStrokeAdjust(GfxState *state) override;
    //void updateFillColorSpace(GfxState *state) override;
    //void updateStrokeColorSpace(GfxState *state) override;
    //void updateFillColor(GfxState *state) override;
    //void updateStrokeColor(GfxState *state) override;
    //void updateBlendMode(GfxState *state) override;
    //void updateFillOpacity(GfxState *state) override;
    //void updateStrokeOpacity(GfxState *state) override;
    //void updatePatternOpacity(GfxState *state) override;
    //void clearPatternOpacity(GfxState *state) override;
    //void updateFillOverprint(GfxState *state) override;
    //void updateStrokeOverprint(GfxState *state) override;
    //void updateOverprintMode(GfxState *state) override;
    //void updateTransfer(GfxState *state) override;
    //----- update text state
    void updateFont(GfxState *state) override
    {
        fontChanged = true;
        if (transparent)
            SplashOutputDev::updateFont(state);
    }

    //----- path painting
    void stroke(GfxState *state) override
    {
        auto contourDraw = std::make_shared<ContourDraw>();
        contourDraw->contourData = sharedContour(state, *state->getPath());
        contourDraw->strokeColor.setColor(state->getStrokeColorSpace(), state->getStrokeColor()->c);
        contourDraw->lineWidth = state->getLineWidth();
        contourDraw->transform << state->getCTM();

        drawLaiers.push_back(contourDraw);

        if (transparent)
            SplashOutputDev::stroke(state);
    }

    void fill(GfxState *state) override
    {
        auto contourDraw = std::make_shared<ContourDraw>();
        contourDraw->contourData = sharedContour(state, *state->getPath());
        contourDraw->fillColor.setColor(state->getFillColorSpace(), state->getFillColor()->c);
        contourDraw->lineWidth = state->getLineWidth();
        contourDraw->transform << state->getCTM();

        drawLaiers.push_back(contourDraw);


        if (transparent)
            SplashOutputDev::fill(state);
    }

    void eoFill(GfxState *state) override
    {
        if (transparent)
            SplashOutputDev::eoFill(state);
    }

    //GBool tilingPatternFill(GfxState *state, Gfx *gfx, Catalog *catalog, Object *str,
    //                        double *pmat, int paintType, int tilingType, Dict *resDict,
    //                        double *mat, double *bbox,
    //                        int x0, int y0, int x1, int y1,
    //                        double xStep, double yStep) override;
    //GBool functionShadedFill(GfxState *state, GfxFunctionShading *shading) override;
    //GBool axialShadedFill(GfxState *state, GfxAxialShading *shading, double tMin, double tMax) override;
    //GBool radialShadedFill(GfxState *state, GfxRadialShading *shading, double tMin, double tMax) override;
    //GBool gouraudTriangleShadedFill(GfxState *state, GfxGouraudTriangleShading *shading) override;

    ////----- path clipping
    //void clip(GfxState *state) override;
    //void eoClip(GfxState *state) override;
    //void clipToStrokePath(GfxState *state) override;

    ////----- text drawing
    void drawChar(GfxState *state, double x, double y,
                  double dx, double dy,
                  double originX, double originY,
                  CharCode code, int nBytes, Unicode *u, int uLen) override
    {
        auto render = state->getRender();
        if (render == 3)
            return;

        auto contourDraw = std::make_shared<ContourDraw>();
        contourDraw->contourData = sharedContour(state, code);
        contourDraw->clip = (render >= 4) ? true : false;
        if (render == 0 || render == 2 || render == 4 || render == 6)
            contourDraw->fillColor.setColor(state->getFillColorSpace(), state->getFillColor()->c);

        if (render == 1 || render == 2 || render == 5 || render == 6)
            contourDraw->strokeColor.setColor(state->getStrokeColorSpace(), state->getStrokeColor()->c);
        
        contourDraw->lineWidth = state->getLineWidth();

        {
            Point origin = {x - originX, y - originY};
            TransformationMatrix m;
            m << state->getCTM();
            contourDraw->transform = origin + m;
        }
        drawLaiers.push_back(contourDraw);

        if (transparent)
            SplashOutputDev::drawChar(state, x, y, dx, dy, originX, originY, code, nBytes, u, uLen);
    }
    //GBool beginType3Char(GfxState *state, double x, double y,
    //                     double dx, double dy,
    //                     CharCode code, Unicode *u, int uLen) override;
    //void endType3Char(GfxState *state) override;
    void beginTextObject(GfxState *state) override
    {
        drawLaiers.push_back(std::make_shared<TextStart>());
        if (transparent)
            SplashOutputDev::beginTextObject(state);
    }
    void endTextObject(GfxState *state) override
    {
        drawLaiers.push_back(std::make_shared<TextStop>());
        if (transparent)
            SplashOutputDev::endTextObject(state);
    }

    //----- image drawing
    //void setSoftMaskFromImageMask(GfxState *state,
    //                              Object *ref, Stream *str,
    //                              int width, int height, GBool invert,
    //                              GBool inlineImg, double *baseMatrix) override;
    //void unsetSoftMaskFromImageMask(GfxState *state, double *baseMatrix) override;
    void drawImageMask(GfxState *state, Object *ref, Stream *str,
                       int width, int height, GBool invert,
                       GBool interpolate, GBool inlineImg) override
    {
        auto imageDraw = std::make_shared<ImageDraw>();
        imageDraw->mask = sharedMask(ref, str, width, height, inlineImg);
        imageDraw->fillColor.setColor(state->getFillColorSpace(), state->getFillColor()->c);
        imageDraw->setMaskInversion(invert);
        imageDraw->transform << state->getCTM();
        drawLaiers.push_back(imageDraw);

        if (transparent && !inlineImg)
            SplashOutputDev::drawImageMask(state, ref, str, width, height, invert, interpolate, inlineImg);
    }

    void drawImage(GfxState *state, Object *ref, Stream *str,
                   int width, int height, GfxImageColorMap *colorMap,
                   GBool interpolate, int *maskColors, GBool inlineImg) override
    {
        auto imageDraw = std::make_shared<ImageDraw>();
        imageDraw->image = sharedImage(ref, str, width, height, colorMap, inlineImg);
        if (maskColors)
            imageDraw->maskColors.assign(maskColors, maskColors + 2 * imageDraw->image->channels);

        imageDraw->transform << state->getCTM();
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
        auto imageDraw = std::make_shared<ImageDraw>();
        imageDraw->image = sharedImage(ref, str, width, height, colorMap);
        imageDraw->mask = sharedMask(ref, maskStr, maskWidth, maskHeight);
        imageDraw->setMaskInversion(maskInvert);
        imageDraw->transform << state->getCTM();
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
        auto imageDraw = std::make_shared<ImageDraw>();
        imageDraw->image = sharedImage(ref, str, width, height, colorMap);
        imageDraw->mask = sharedMask(ref, maskStr, maskWidth, maskHeight, maskColorMap);
        imageDraw->transform << state->getCTM();
        drawLaiers.push_back(imageDraw);
        if (transparent)
            SplashOutputDev::drawSoftMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskColorMap, maskInterpolate);
    }

    ////----- Type 3 font operators
    //void type3D0(GfxState *state, double wx, double wy) override;
    //void type3D1(GfxState *state, double wx, double wy,
    //             double llx, double lly, double urx, double ury) override;

    ////----- transparency groups and soft masks
    //GBool checkTransparencyGroup(GfxState *state, GBool knockout) override;
    //void beginTransparencyGroup(GfxState *state, double *bbox,
    //                            GfxColorSpace *blendingColorSpace,
    //                            GBool isolated, GBool knockout,
    //                            GBool forSoftMask) override;
    //void endTransparencyGroup(GfxState *state) override;
    //void paintTransparencyGroup(GfxState *state, double *bbox) override;
    //void setSoftMask(GfxState *state, double *bbox, GBool alpha,
    //                 Function *transferFunc, GfxColor *backdropColor) override;
    //void clearSoftMask(GfxState *state) override;

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
