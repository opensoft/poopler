#include "ExternDrawOutputDivice.h"

#include <splash/SplashGlyphBitmap.h>

namespace poppler {
TransformationMatrix &operator<<(TransformationMatrix &t, const double *ctm)
{
    std::copy(ctm, ctm + 6, t.begin());
    return t;
}

ColorPrivate::ColorPrivate(const GfxColorSpace *colorSpace, const GfxColor *color, double opacity)
{
    if (colorSpace->isNonMarking())
        return;

    const int *colors16_16 = color->c;
    format = formatCast(const_cast<GfxColorSpace *>(colorSpace)->getMode());
    for (int i = 0; i < colorSpace->getNComps(); i++)
        components.push_back(colToDbl(colors16_16[i]));

    components.push_back(opacity);
}

Color::Format ColorPrivate::formatCast(const GfxColorSpaceMode &gfxColorSpaceMode)
{
    switch (gfxColorSpaceMode) {
    case csDeviceGray:
        return DeviceGray;
    case csDeviceRGB:
        return DeviceRGB;
    case csDeviceCMYK:
        return DeviceCMYK;
    case csCalGray:
        return CalGray;
    case csCalRGB:
        return CalRGB;
    case csLab:
        return Lab;
    case csICCBased:
        return ICCBased;
    case csIndexed:
        return Indexed;
    case csSeparation:
        return Separation;
    case csDeviceN:
        return DeviceN;
    case csPattern:
        return Pattern;
    default:
        break;
    }
    return DeviceGray;
}

ImageDataPrivate::ImageDataPrivate(bool isAskForImage, Stream *str, int _width, int _height, GfxImageColorMap *colorMap,
                                   bool inlineImg)
{
    width = _width;
    height = _height;
    bitsPerChannel = colorMap->getBits();
    channels = colorMap->getNumPixelComps();
    rowSize = (bitsPerChannel * channels * width + 7) / 8;
    format = ColorPrivate::formatCast(colorMap->getColorSpace()->getMode());
    if (isAskForImage)
        download(str, inlineImg);
}

ImageDataPrivate::ImageDataPrivate(bool isAskForImage, Stream *str, int _width, int _height, bool inlineImg)
{
    width = _width;
    height = _height;
    bitsPerChannel = 1;
    channels = 1;
    rowSize = (bitsPerChannel * channels * width + 7) / 8;
    format = Color::DeviceGray;
    if (isAskForImage)
        download(str, inlineImg);
}

ImageDataPrivate::ImageDataPrivate(bool isAskForImage, const SplashGlyphBitmap &rasterGlyph)
{
    height = rasterGlyph.h;
    width = rasterGlyph.w;
    bitsPerChannel = (rasterGlyph.aa) ? 8 : 1;
    channels = 1;
    rowSize = (rasterGlyph.aa) ? rasterGlyph.w : ((rasterGlyph.w + 7) / 8);
    format = Color::DeviceGray;
    if (isAskForImage)
        data.assign(rasterGlyph.data, rasterGlyph.data + rowSize * height);
}

void ImageDataPrivate::download(Stream *str, bool /*inlineImg*/)
{
    try {
        data.resize(rowSize * height);
    } catch (const std::exception &) {
        data.clear();
        return;
    }
    str->reset();
    str->doGetChars(data.size(), data.data());
    str->close();
}

ContourDataPrivate::ContourDataPrivate(const SplashPath &path)
{
    const int pathLength = const_cast<SplashPath &>(path).getLength();
    points.resize(pathLength);
    for (int i = 0; i < pathLength; ++i) {
        ContourPoint &point = points[i];
        unsigned char flags;
        const_cast<SplashPath &>(path).getPoint(i, &point.x, &point.y, &flags);
        point.firstPoint = flags & splashPathFirst;
        point.lastPoint = flags & splashPathLast;
        point.pathClosed = flags & splashPathClosed;
        point.pathCurve = flags & splashPathCurve;
    }
}

ContourDataPrivate::ContourDataPrivate(const GfxPath &path)
{
    for (int i = 0; i < path.getNumSubpaths(); ++i) {
        GfxSubpath *subpath = const_cast<GfxPath &>(path).getSubpath(i);
        const int pointsNumber = subpath->getNumPoints();
        std::vector<ContourPoint> contour(pointsNumber);
        for (int j = 0, curvePoints = 0; j < pointsNumber; ++j, curvePoints >>= 1) {
            ContourPoint &point = contour[j];
            point.x = subpath->getX(j);
            point.y = subpath->getY(j);
            if (!curvePoints && subpath->getCurve(j))
                curvePoints = 2;
            point.pathCurve = !!curvePoints;
        }
        contour.front().firstPoint = true;
        contour.back().lastPoint = true;
        if (subpath->isClosed())
            contour.back().pathClosed = true;

        points.insert(points.end(), contour.begin(), contour.end());
    }
}

SharedImageData ExternDrawOutputDivice::sharedGlyphImage(const GfxState *state, const CharCode &code)
{
    auto fontReference = const_cast<GfxState *>(state)->getFont()->getID();
    auto reference = PdfCharReference(PdfReference(fontReference->num, fontReference->gen), code);
    auto iterator = m_rasterGlyphs.find(reference);
    if (iterator != m_rasterGlyphs.end() && iterator->second)
        return iterator->second;

    struct SplashGlyphBitmapAutoFree : public SplashGlyphBitmap
    {
        ~SplashGlyphBitmapAutoFree()
        {
            if (freeData)
                gfree(data);
        }
    } glyphBitmap;
    SplashClipResult clipRes;
    SplashClip clp(-1, -1, 100, 100, false);
    if (!getCurrentFont()->makeGlyph(code, 0, 0, &glyphBitmap, 0, 0, &clp, &clipRes))
        return SharedImageData();
    return m_rasterGlyphs[reference] = std::make_shared<ImageDataPrivate>(m_withImageData, glyphBitmap);
}

SharedContourData ExternDrawOutputDivice::sharedContour(const GfxState *state, const CharCode &code)
{
    auto fontReference = const_cast<GfxState *>(state)->getFont()->getID();
    auto reference = PdfCharReference(PdfReference(fontReference->num, fontReference->gen), code);
    auto iterator = m_glyphs.find(reference);
    if (iterator != m_glyphs.end() && iterator->second)
        return iterator->second;
    auto glyphPath = getCurrentFont()->getGlyphPath(code);
    if (!glyphPath)
        return SharedContourData();
    return m_glyphs[reference] = std::make_shared<ContourDataPrivate>(*glyphPath);
}

SharedContourData ExternDrawOutputDivice::sharedContour(const GfxState * /*state*/, const GfxPath &path)
{
    return std::make_shared<ContourDataPrivate>(path);
}

ExternDrawOutputDivice::ExternDrawOutputDivice(bool useSplashDraw, bool withImageData, SplashColorMode colorModeA,
                                               int bitmapRowPadA, bool reverseVideoA, SplashColorPtr paperColorA,
                                               bool bitmapTopDownA, SplashThinLineMode thinLineMode,
                                               bool overprintPreviewA)
    : SplashOutputDev(colorModeA, bitmapRowPadA, reverseVideoA, paperColorA, bitmapTopDownA, thinLineMode,
                      overprintPreviewA)
{
    m_withSplashDraw = useSplashDraw;
    m_withImageData = withImageData;
}

void ExternDrawOutputDivice::saveState(GfxState *state)
{
    drawLaiers.push_back(std::make_shared<StateSave>());
    if (m_withSplashDraw)
        SplashOutputDev::saveState(state);
}

void ExternDrawOutputDivice::restoreState(GfxState *state)
{
    drawLaiers.push_back(std::make_shared<StateRestore>());
    fontChanged = true;
    if (m_withSplashDraw)
        SplashOutputDev::restoreState(state);
}

void ExternDrawOutputDivice::updateAll(GfxState *state)
{
    fontChanged = true;
    if (m_withSplashDraw)
        SplashOutputDev::updateAll(state);
}

void ExternDrawOutputDivice::updateFont(GfxState *state)
{
    fontChanged = true;
    if (m_withSplashDraw)
        SplashOutputDev::updateFont(state);
}

void ExternDrawOutputDivice::stroke(GfxState *state)
{
    auto contourDraw = std::make_shared<ContourDraw>();
    contourDraw->contourData = sharedContour(state, *state->getPath());
    contourDraw->strokeColor = ColorPrivate(state->getStrokeColorSpace(), state->getStrokeColor(),
                                            state->getStrokeOpacity());
    contourDraw->lineWidth = state->getLineWidth();
    contourDraw->transform = currientTransformation(state);

    drawLaiers.push_back(contourDraw);

    if (m_withSplashDraw)
        SplashOutputDev::stroke(state);
}

void ExternDrawOutputDivice::fill(GfxState *state)
{
    auto contourDraw = std::make_shared<ContourDraw>();
    contourDraw->contourData = sharedContour(state, *state->getPath());
    contourDraw->fillColor = ColorPrivate(state->getFillColorSpace(), state->getFillColor(), state->getFillOpacity());
    contourDraw->transform = currientTransformation(state);

    drawLaiers.push_back(contourDraw);

    if (m_withSplashDraw)
        SplashOutputDev::fill(state);
}

void ExternDrawOutputDivice::eoFill(GfxState *state)
{
    auto contourDraw = std::make_shared<ContourDraw>();
    contourDraw->contourData = sharedContour(state, *state->getPath());
    contourDraw->fillColor = ColorPrivate(state->getFillColorSpace(), state->getFillColor(), state->getFillOpacity());
    contourDraw->evenOddFillRule = true;
    contourDraw->transform = currientTransformation(state);

    drawLaiers.push_back(contourDraw);

    if (m_withSplashDraw)
        SplashOutputDev::eoFill(state);
}

void ExternDrawOutputDivice::drawChar(GfxState *state, double x, double y, double dx, double dy, double originX,
                                      double originY, CharCode code, int nBytes, Unicode *u, int uLen)
{
    auto render = state->getRender();
    if (render == 3)
        return;
    if (fontChanged) {
        doUpdateFont(const_cast<GfxState *>(state));
        fontChanged = false;
        // This is a hack because splash font make glyph contours
        // using textCTM, font size and zoom instead making it with identity matrix.
        // That is why splash glyphs with the same code and font are not identical.
        m_glyphs.clear();
        m_rasterGlyphs.clear();
    }
    auto glyphDraw = std::make_shared<GlyphDraw>();
    glyphDraw->contourData = sharedContour(state, code);
    if (!glyphDraw->contourData)
        glyphDraw->image = sharedGlyphImage(state, code);

    glyphDraw->unicode = (u && uLen > 0) ? *u : 0;
    glyphDraw->clip = (render >= 4) ? true : false;
    glyphDraw->lineWidth = state->getLineWidth();
    Point origin = {x - originX, y - originY};
    glyphDraw->transform = origin + currientTransformation(state);

    if (render == 0 || render == 2 || render == 4 || render == 6)
        glyphDraw->fillColor = ColorPrivate(state->getFillColorSpace(), state->getFillColor(), state->getFillOpacity());

    if (render == 1 || render == 2 || render == 5 || render == 6)
        glyphDraw->strokeColor = ColorPrivate(state->getStrokeColorSpace(), state->getStrokeColor(),
                                              state->getStrokeOpacity());

    drawLaiers.push_back(glyphDraw);

    if (m_withSplashDraw)
        SplashOutputDev::drawChar(state, x, y, dx, dy, originX, originY, code, nBytes, u, uLen);
}

void ExternDrawOutputDivice::beginTextObject(GfxState *state)
{
    drawLaiers.push_back(std::make_shared<TextStart>());
    if (m_withSplashDraw)
        SplashOutputDev::beginTextObject(state);
}

void ExternDrawOutputDivice::endTextObject(GfxState *state)
{
    drawLaiers.push_back(std::make_shared<TextStop>());
    if (m_withSplashDraw)
        SplashOutputDev::endTextObject(state);
}

void ExternDrawOutputDivice::drawImageMask(GfxState *state, Object *ref, Stream *str, int width, int height,
                                           bool invert, bool interpolate, bool inlineImg)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->mask = sharedMask(ref, str, width, height, inlineImg);
    imageDraw->fillColor = ColorPrivate(state->getFillColorSpace(), state->getFillColor(), state->getFillOpacity());
    imageDraw->setMaskInversion(invert);
    imageDraw->transform = currientTransformation(state);
    drawLaiers.push_back(imageDraw);

    if (m_withSplashDraw && !inlineImg)
        SplashOutputDev::drawImageMask(state, ref, str, width, height, invert, interpolate, inlineImg);
}

void ExternDrawOutputDivice::drawImage(GfxState *state, Object *ref, Stream *str, int width, int height,
                                       GfxImageColorMap *colorMap, bool interpolate, int *maskColors, bool inlineImg)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->image = sharedImage(ref, str, width, height, colorMap, inlineImg);
    if (maskColors)
        imageDraw->maskColors.assign(maskColors, maskColors + 2 * imageDraw->image->channels);

    imageDraw->transform = currientTransformation(state);
    drawLaiers.push_back(imageDraw);

    if (m_withSplashDraw && !inlineImg)
        SplashOutputDev::drawImage(state, ref, str, width, height, colorMap, interpolate, maskColors, inlineImg);
}

void ExternDrawOutputDivice::drawMaskedImage(GfxState *state, Object *ref, Stream *str, int width, int height,
                                             GfxImageColorMap *colorMap, bool interpolate, Stream *maskStr,
                                             int maskWidth, int maskHeight, bool maskInvert, bool maskInterpolate)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->image = sharedImage(ref, str, width, height, colorMap);
    imageDraw->mask = sharedMask(ref, maskStr, maskWidth, maskHeight);
    imageDraw->setMaskInversion(maskInvert);
    imageDraw->transform = currientTransformation(state);
    drawLaiers.push_back(imageDraw);

    if (m_withSplashDraw)
        SplashOutputDev::drawMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth,
                                         maskHeight, maskInvert, maskInterpolate);
}

void ExternDrawOutputDivice::drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str, int width, int height,
                                                 GfxImageColorMap *colorMap, bool interpolate, Stream *maskStr,
                                                 int maskWidth, int maskHeight, GfxImageColorMap *maskColorMap,
                                                 bool maskInterpolate)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->image = sharedImage(ref, str, width, height, colorMap);
    imageDraw->mask = sharedMask(ref, maskStr, maskWidth, maskHeight, maskColorMap);
    imageDraw->transform = currientTransformation(state);
    drawLaiers.push_back(imageDraw);
    if (m_withSplashDraw)
        SplashOutputDev::drawSoftMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth,
                                             maskHeight, maskColorMap, maskInterpolate);
}

void ExternDrawOutputDivice::beginTransparencyGroup(GfxState *state, const double *bbox,
                                                    GfxColorSpace *blendingColorSpace, bool isolated, bool knockout,
                                                    bool forSoftMask)
{
    drawLaiers.push_back(std::make_shared<TransparencyGroupStart>());
    TransformationMatrix matrix;
    matrix << state->getCTM();
    m_groupStack.push_back(matrix);
    if (m_withSplashDraw)
        SplashOutputDev::beginTransparencyGroup(state, bbox, blendingColorSpace, isolated, knockout, forSoftMask);
}

void ExternDrawOutputDivice::endTransparencyGroup(GfxState *state)
{
    drawLaiers.push_back(std::make_shared<TransparencyGroupStop>());
    m_groupStack.pop_back();
    if (m_withSplashDraw)
        SplashOutputDev::endTransparencyGroup(state);
}

void ExternDrawOutputDivice::paintTransparencyGroup(GfxState *state, const double *bbox)
{
    drawLaiers.push_back(std::make_shared<TransparencyGroupDraw>());
    if (m_withSplashDraw)
        SplashOutputDev::paintTransparencyGroup(state, bbox);
}

poppler::TransformationMatrix ExternDrawOutputDivice::currientTransformation(GfxState *state)
{
    TransformationMatrix transformation;
    if (m_groupStack.empty())
        transformation << state->getCTM();
    else
        transformation = m_groupStack.front();
    return transformation;
}
} // namespace poppler
