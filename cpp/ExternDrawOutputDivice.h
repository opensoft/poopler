#ifndef EXTERNDRAWOUTPUTDIVICE_H
#define EXTERNDRAWOUTPUTDIVICE_H

#include "poppler-extern-draw.h"
#include <config.h>

#include "GfxFont.h"
#if defined(HAVE_SPLASH)
#include "SplashOutputDev.h"
#include "splash/Splash.h"
#include "splash/SplashBitmap.h"
#include "splash/SplashFont.h"
#include "splash/SplashFontFile.h"
#include "splash/SplashPath.h"
#endif

#include <map>

namespace poppler {

TransformationMatrix &operator<<(TransformationMatrix &t, const double * ctm);

using PdfReference = std::pair<int, int>;
using PdfCharReference = std::pair<PdfReference, decltype(GlyphDraw::unicode)>;
using SharedImageData = std::shared_ptr<ImageData>;

using ImageStore = std::map<PdfReference, SharedImageData>;
using GlyphStore = std::map<PdfCharReference, SharedContourData>;

struct ColorPrivate : public Color
{
    ColorPrivate(/*const*/ GfxColorSpace &colorSpace, /*const*/ GfxColor &color);
    static Color::Format formatCast(const GfxColorSpaceMode &gfxColorSpaceMode);
};

struct ImageDataPrivate : public ImageData
{
    ImageDataPrivate(Stream *str, int _width, int _height, GfxImageColorMap *colorMap, GBool inlineImg = gFalse);
    ImageDataPrivate(Stream *str, int _width, int _height, GBool inlineImg = gFalse);
    void download(Stream *str, GBool inlineImg);
};

struct ContourDataPrivate : public ContourData
{
    ContourDataPrivate(/*const*/ SplashPath &path);
    ContourDataPrivate(/*const*/ GfxPath &path);
};

struct ImageDrawPrivate : public ImageDraw
{
    void setMaskInversion(bool invert)
    {
        if (invert)
            maskColors = {1, 0};
        else
            maskColors = {0, 1};
    }
};
class ExternDrawOutputDivice : public SplashOutputDev
{
    bool transparent = true;

    ImageStore m_images;
    ImageStore m_imageMasks;
    GlyphStore m_glyphs;
    //std::list<SharedImageData> m_inlineImages;

    ProcessStepStore drawLaiers;

    template<typename ... Args>
    SharedImageData sharedImage(ImageStore &imageStore, Object *ref, Args && ...args)
    {
        PdfReference reference;
        if (ref && ref->isRef()) {
            reference = PdfReference(ref->getRef().num, ref->getRef().gen);
            auto imageIterator = imageStore.find(reference);
            if (imageIterator != imageStore.end() && imageIterator->second)
                return imageIterator->second;
        }
        auto sharedImageData = std::make_shared<ImageDataPrivate>(args...);
        if (ref && ref->isRef())
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
    SharedContourData sharedContour(/*const*/ GfxState *state, const CharCode &code);
    SharedContourData sharedContour(/*const*/ GfxState *state, /*const*/ GfxPath &path);
public:
    ProcessStepStore drawingSteps() const
    {
        return drawLaiers;
    }
    ExternDrawOutputDivice(bool useSplashDraw, SplashColorMode colorModeA, int bitmapRowPadA,
                           GBool reverseVideoA, SplashColorPtr paperColorA,
                           GBool bitmapTopDownA = gTrue,
                           SplashThinLineMode thinLineMode = splashThinLineDefault,
                           GBool overprintPreviewA = globalParams->getOverprintPreview());
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
    void saveState(GfxState *state) override;
    void restoreState(GfxState *state) override;
    //----- update graphics state
    void updateAll(GfxState *state) override;
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
    void updateFont(GfxState *state) override;

    //----- path painting
    void stroke(GfxState *state) override;
    void fill(GfxState *state) override;
    void eoFill(GfxState *state) override;

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
                  CharCode code, int nBytes, Unicode *u, int uLen) override;
    //GBool beginType3Char(GfxState *state, double x, double y,
    //                     double dx, double dy,
    //                     CharCode code, Unicode *u, int uLen) override;
    //void endType3Char(GfxState *state) override;
    void beginTextObject(GfxState *state) override;
    void endTextObject(GfxState *state) override;

    //----- image drawing
    //void setSoftMaskFromImageMask(GfxState *state,
    //                              Object *ref, Stream *str,
    //                              int width, int height, GBool invert,
    //                              GBool inlineImg, double *baseMatrix) override;
    //void unsetSoftMaskFromImageMask(GfxState *state, double *baseMatrix) override;
    void drawImageMask(GfxState *state, Object *ref, Stream *str,
                       int width, int height, GBool invert,
                       GBool interpolate, GBool inlineImg) override;

    void drawImage(GfxState *state, Object *ref, Stream *str,
                   int width, int height, GfxImageColorMap *colorMap,
                   GBool interpolate, int *maskColors, GBool inlineImg) override;

    void drawMaskedImage(GfxState *state, Object *ref, Stream *str,
                         int width, int height,
                         GfxImageColorMap *colorMap,
                         GBool interpolate,
                         Stream *maskStr, int maskWidth, int maskHeight,
                         GBool maskInvert, GBool maskInterpolate) override;

    void drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str,
                             int width, int height,
                             GfxImageColorMap *colorMap,
                             GBool interpolate,
                             Stream *maskStr,
                             int maskWidth, int maskHeight,
                             GfxImageColorMap *maskColorMap,
                             GBool maskInterpolate) override;

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
}


#endif // EXTERNDRAWOUTPUTDIVICE_H
