#ifndef ExternDrawOutputDevice_H
#define ExternDrawOutputDevice_H

#include "GfxFont.h"
#include "poppler-extern-draw.h"

#include <config.h>
#if defined(HAVE_SPLASH)
#    include "SplashOutputDev.h"
#    include "splash/Splash.h"
#    include "splash/SplashBitmap.h"
#    include "splash/SplashFont.h"
#    include "splash/SplashFontFile.h"
#    include "splash/SplashPath.h"
#endif

#include <deque>
#include <map>

namespace poppler {
TransformationMatrix &operator<<(TransformationMatrix &t, const double *ctm);

using PdfReference = std::pair<int, int>;
using PdfCharReference = std::pair<PdfReference, decltype(GlyphDraw::unicode)>;

using ImageStore = std::map<PdfReference, SharedImageData>;
using GlyphStore = std::map<PdfCharReference, SharedContourData>;
using RasterGlyphStore = std::map<PdfCharReference, SharedImageData>;

struct ColorPrivate : public Color
{
    ColorPrivate(const GfxColorSpace *colorSpace, const GfxColor *color, double opacity);
    static Color::Format formatCast(const GfxColorSpaceMode &gfxColorSpaceMode);
};

struct ImageDataPrivate : public ImageData
{
    ImageDataPrivate(bool isAskForImage, Stream *str, int _width, int _height, GfxImageColorMap *colorMap,
                     bool inlineImg = false);
    ImageDataPrivate(bool isAskForImage, Stream *str, int _width, int _height, bool inlineImg = false);
    ImageDataPrivate(bool isAskForImage, const SplashGlyphBitmap &rasterGlyph);

private:
    void download(Stream *str, bool inlineImg);
};

struct ContourDataPrivate : public ContourData
{
    ContourDataPrivate(const SplashPath &path);
    ContourDataPrivate(const GfxPath &path);
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

class ExternDrawOutputDevice : public SplashOutputDev
{
    bool m_withSplashDraw = true;
    bool m_withImageData = true;
    //the same as the parent's needFontUpdate which is not accessible
    bool fontChanged = true;

    ImageStore m_images;
    ImageStore m_imageMasks;
    GlyphStore m_glyphs;
    RasterGlyphStore m_rasterGlyphs;
    //std::list<SharedImageData> m_inlineImages;
    std::deque<TransformationMatrix> m_groupStack;

    ProcessStepStore drawLaiers;

    template <typename... Args>
    SharedImageData sharedImage(ImageStore &imageStore, Object *ref, Args &&... args)
    {
        PdfReference reference;
        if (ref && ref->isRef()) {
            reference = PdfReference(ref->getRef().num, ref->getRef().gen);
            auto imageIterator = imageStore.find(reference);
            if (imageIterator != imageStore.end() && imageIterator->second)
                return imageIterator->second;
        }
        auto sharedImageData = std::make_shared<ImageDataPrivate>(m_withImageData, args...);
        if (ref && ref->isRef())
            imageStore[reference] = sharedImageData;
        //else
        //    m_inlineImages.push_back(sharedImageData);

        return sharedImageData;
    }
    template <typename... Args>
    SharedImageData sharedImage(Args &&... args)
    {
        return sharedImage(m_images, args...);
    }

    template <typename... Args>
    SharedImageData sharedMask(Args &&... args)
    {
        return sharedImage(m_imageMasks, args...);
    }

    SharedImageData sharedGlyphImage(const GfxState *state, const CharCode &code);
    SharedContourData sharedContour(const GfxState *state, const CharCode &code);
    SharedContourData sharedContour(const GfxState *state, const GfxPath &path);
    TransformationMatrix currentTransformation(GfxState *state);

public:
    ProcessStepStore drawingSteps() const { return drawLaiers; }
    ExternDrawOutputDevice(bool useSplashDraw, bool withImageData, SplashColorMode colorModeA, int bitmapRowPadA,
                           bool reverseVideoA, SplashColorPtr paperColorA, bool bitmapTopDownA = true,
                           SplashThinLineMode thinLineMode = splashThinLineDefault,
                           bool overprintPreviewA = globalParams->getOverprintPreview());
    ////----- get info about output device
    //// Does this device use tilingPatternFill()?  If this returns false,
    //// tiling pattern fills will be reduced to a series of other drawing
    //// operations.
    //bool useTilingPatternFill() override
    //{
    //    return true;
    //}
    //// Does this device use functionShadedFill(), axialShadedFill(), and
    //// radialShadedFill()?  If this returns false, these shaded fills
    //// will be reduced to a series of other drawing operations.
    //bool useShadedFills(int type) override
    //{
    //    return (type >= 1 && type <= 5) ? true : false;
    //}
    //// Does this device use upside-down coordinates?
    //// (Upside-down means (0,0) is the top left corner of the page.)
    //bool upsideDown() override
    //{
    //    return bitmapTopDown ^ bitmapUpsideDown;
    //}
    //// Does this device use drawChar() or drawString()?
    //bool useDrawChar() override
    //{
    //    return true;
    //}
    //// Does this device use beginType3Char/endType3Char?  Otherwise,
    //// text in Type 3 fonts will be drawn with drawChar/drawString.
    //bool interpretType3Chars() override
    //{
    //    return true;
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

    //bool tilingPatternFill(GfxState *state, Gfx *gfx, Catalog *catalog, Object *str,
    //                        double *pmat, int paintType, int tilingType, Dict *resDict,
    //                        double *mat, double *bbox,
    //                        int x0, int y0, int x1, int y1,
    //                        double xStep, double yStep) override;
    //bool functionShadedFill(GfxState *state, GfxFunctionShading *shading) override;
    //bool axialShadedFill(GfxState *state, GfxAxialShading *shading, double tMin, double tMax) override;
    //bool radialShadedFill(GfxState *state, GfxRadialShading *shading, double tMin, double tMax) override;
    //bool gouraudTriangleShadedFill(GfxState *state, GfxGouraudTriangleShading *shading) override;

    ////----- path clipping
    //void clip(GfxState *state) override;
    //void eoClip(GfxState *state) override;
    //void clipToStrokePath(GfxState *state) override;

    ////----- text drawing
    void drawChar(GfxState *state, double x, double y, double dx, double dy, double originX, double originY,
                  CharCode code, int nBytes, Unicode *u, int uLen) override;
    //bool beginType3Char(GfxState *state, double x, double y,
    //                     double dx, double dy,
    //                     CharCode code, Unicode *u, int uLen) override;
    //void endType3Char(GfxState *state) override;
    void beginTextObject(GfxState *state) override;
    void endTextObject(GfxState *state) override;

    //----- image drawing
    //void setSoftMaskFromImageMask(GfxState *state,
    //                              Object *ref, Stream *str,
    //                              int width, int height, bool invert,
    //                              bool inlineImg, double *baseMatrix) override;
    //void unsetSoftMaskFromImageMask(GfxState *state, double *baseMatrix) override;
    void drawImageMask(GfxState *state, Object *ref, Stream *str, int width, int height, bool invert, bool interpolate,
                       bool inlineImg) override;

    void drawImage(GfxState *state, Object *ref, Stream *str, int width, int height, GfxImageColorMap *colorMap,
                   bool interpolate, int *maskColors, bool inlineImg) override;

    void drawMaskedImage(GfxState *state, Object *ref, Stream *str, int width, int height, GfxImageColorMap *colorMap,
                         bool interpolate, Stream *maskStr, int maskWidth, int maskHeight, bool maskInvert,
                         bool maskInterpolate) override;

    void drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str, int width, int height,
                             GfxImageColorMap *colorMap, bool interpolate, Stream *maskStr, int maskWidth,
                             int maskHeight, GfxImageColorMap *maskColorMap, bool maskInterpolate) override;

    ////----- Type 3 font operators
    //void type3D0(GfxState *state, double wx, double wy) override;
    //void type3D1(GfxState *state, double wx, double wy,
    //             double llx, double lly, double urx, double ury) override;

    ////----- transparency groups and soft masks
    //bool checkTransparencyGroup(GfxState *state, bool knockout) override;
    void beginTransparencyGroup(GfxState *state, const double *bbox, GfxColorSpace *blendingColorSpace, bool isolated,
                                bool knockout, bool forSoftMask) override;

    void endTransparencyGroup(GfxState *state) override;
    void paintTransparencyGroup(GfxState *state, const double *bbox) override;
    //void setSoftMask(GfxState *state, double *bbox, bool alpha,
    //                 Function *transferFunc, GfxColor *backdropColor) override;
    //void clearSoftMask(GfxState *state) override;

protected:
private:
};
} // namespace poppler

#endif // ExternDrawOutputDevice_H
