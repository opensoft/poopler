#include "ExternDrawOutputDivice.h"

namespace poppler {

TransformationMatrix &operator<<(TransformationMatrix &t, const double * ctm)
{
    std::copy(ctm, ctm + 6, t.begin());
    return t;
}

ColorPrivate::ColorPrivate(/*const*/ GfxColorSpace &colorSpace, /*const*/ GfxColor &color, const double &opacity)
{
    if (colorSpace.isNonMarking())
        return;

    int *colors16_16 = color.c;
    format = formatCast(colorSpace.getMode());
    for (int i = 0; i < colorSpace.getNComps(); i++)
        components.push_back(colToDbl(colors16_16[i]));

    components.push_back(opacity);
}

Color::Format ColorPrivate::formatCast(const GfxColorSpaceMode &gfxColorSpaceMode)
{
    switch (gfxColorSpaceMode) {
    case csDeviceGray: return DeviceGray;
    case csDeviceRGB: return DeviceRGB;
    case csDeviceCMYK: return DeviceCMYK;
    case csCalGray: return CalGray;
    case csCalRGB: return CalRGB;
    case csLab: return Lab;
    case csICCBased: return ICCBased;
    case csIndexed: return Indexed;
    case csSeparation: return Separation;
    case csDeviceN: return DeviceN;
    case csPattern: return Pattern;
    default: break;
    }
    return DeviceGray;
}

ImageDataPrivate::ImageDataPrivate(bool isAskForImage, Stream *str, int _width, int _height, GfxImageColorMap *colorMap, GBool inlineImg /*= gFalse*/)
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

ImageDataPrivate::ImageDataPrivate(bool isAskForImage, Stream *str, int _width, int _height, GBool inlineImg /*= gFalse*/)
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

void ImageDataPrivate::download(Stream *str, GBool /*inlineImg*/)
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

ContourDataPrivate::ContourDataPrivate(/*const*/ SplashPath &path)
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

ContourDataPrivate::ContourDataPrivate(/*const*/ GfxPath &path)
{
    for (int i = 0; i < path.getNumSubpaths(); ++i) {
        std::vector<ContourPoint> contour;
        GfxSubpath *subpath = path.getSubpath(i);
        contour.reserve(subpath->getNumPoints());
        for (int j = 0, curvePoints = 0; j < subpath->getNumPoints(); ++j, curvePoints >>= 1) {
            if (!curvePoints && subpath->getCurve(j))
                curvePoints = 2;

            ContourPoint point;
            point.x = subpath->getX(j);
            point.y = subpath->getY(j);
            point.pathCurve = !!curvePoints;
            contour.push_back(point);
        }
        contour.front().firstPoint = true;
        contour.back().lastPoint = true;
        if (subpath->isClosed())
            contour.back().pathClosed = true;

        points.insert(points.end(), contour.begin(), contour.end());
    }
}

SharedContourData ExternDrawOutputDivice::sharedContour(/*const*/ GfxState *state, const CharCode &code)
{
    if (fontChanged) {
        doUpdateFont(state);
        fontChanged = false;
        // This is a hack because splash font make glyph contours
        // using textCTM, font size and zoom instead making it with identity matrix.
        // That is why splash glyphs with same code and font are not identical.
        m_glyphs.clear();
    }
    auto fontReference = state->getFont()->getID();
    auto reference = PdfCharReference(PdfReference(fontReference->num, fontReference->gen), code);
    auto iterator = m_glyphs.find(reference);
    if (iterator != m_glyphs.end() && iterator->second)
        return iterator->second;

    return m_glyphs[reference] =
        std::make_shared<ContourDataPrivate>(*getCurrentFont()->getGlyphPath(code));
}

SharedContourData ExternDrawOutputDivice::sharedContour(/*const*/ GfxState *state, /*const*/ GfxPath &path)
{
    return std::make_shared<ContourDataPrivate>(path);
}

ExternDrawOutputDivice::ExternDrawOutputDivice(bool useSplashDraw, bool withImageData, SplashColorMode colorModeA, int bitmapRowPadA,
                                               GBool reverseVideoA, SplashColorPtr paperColorA,
                                               GBool bitmapTopDownA, SplashThinLineMode thinLineMode,
                                               GBool overprintPreviewA)
    : SplashOutputDev(colorModeA, bitmapRowPadA, reverseVideoA, paperColorA,
                      bitmapTopDownA, thinLineMode, overprintPreviewA)
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
    contourDraw->strokeColor = ColorPrivate(*state->getStrokeColorSpace(), *state->getStrokeColor(), state->getStrokeOpacity());
    contourDraw->lineWidth = state->getLineWidth();
    contourDraw->transform << state->getCTM();

    drawLaiers.push_back(contourDraw);

    if (m_withSplashDraw)
        SplashOutputDev::stroke(state);
}

void ExternDrawOutputDivice::fill(GfxState *state)
{
    auto contourDraw = std::make_shared<ContourDraw>();
    contourDraw->contourData = sharedContour(state, *state->getPath());
    contourDraw->fillColor = ColorPrivate(*state->getFillColorSpace(), *state->getFillColor(), state->getFillOpacity());
    contourDraw->transform << state->getCTM();

    drawLaiers.push_back(contourDraw);

    if (m_withSplashDraw)
        SplashOutputDev::fill(state);
}

void ExternDrawOutputDivice::eoFill(GfxState *state)
{
    auto contourDraw = std::make_shared<ContourDraw>();
    contourDraw->contourData = sharedContour(state, *state->getPath());
    contourDraw->fillColor = ColorPrivate(*state->getFillColorSpace(), *state->getFillColor(), state->getFillOpacity());
    contourDraw->evenOddFillRule = true;
    contourDraw->transform << state->getCTM();

    drawLaiers.push_back(contourDraw);

    if (m_withSplashDraw)
        SplashOutputDev::eoFill(state);
}

void ExternDrawOutputDivice::drawChar(GfxState *state, double x, double y, double dx, double dy, double originX, double originY, CharCode code, int nBytes, Unicode *u, int uLen)
{
    auto render = state->getRender();
    if (render == 3)
        return;

    auto contourDraw = std::make_shared<GlyphDraw>();
    contourDraw->unicode = *u;
    contourDraw->clip = (render >= 4) ? true : false;
    contourDraw->contourData = sharedContour(state, code);
    contourDraw->lineWidth = state->getLineWidth();
    Point origin = {x - originX, y - originY};
    TransformationMatrix m;
    m << state->getCTM();
    contourDraw->transform = origin + m;

    if (render == 0 || render == 2 || render == 4 || render == 6)
        contourDraw->fillColor = ColorPrivate(*state->getFillColorSpace(), *state->getFillColor(), state->getFillOpacity());

    if (render == 1 || render == 2 || render == 5 || render == 6)
        contourDraw->strokeColor = ColorPrivate(*state->getStrokeColorSpace(), *state->getStrokeColor(), state->getStrokeOpacity());

    drawLaiers.push_back(contourDraw);

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

void ExternDrawOutputDivice::drawImageMask(GfxState *state, Object *ref, Stream *str, int width, int height, GBool invert, GBool interpolate, GBool inlineImg)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->mask = sharedMask(ref, str, width, height, inlineImg);
    imageDraw->fillColor = ColorPrivate(*state->getFillColorSpace(), *state->getFillColor(), state->getFillOpacity());
    imageDraw->setMaskInversion(invert);
    imageDraw->transform << state->getCTM();
    drawLaiers.push_back(imageDraw);

    if (m_withSplashDraw && !inlineImg)
        SplashOutputDev::drawImageMask(state, ref, str, width, height, invert, interpolate, inlineImg);
}

void ExternDrawOutputDivice::drawImage(GfxState *state, Object *ref, Stream *str, int width, int height, GfxImageColorMap *colorMap, GBool interpolate, int *maskColors, GBool inlineImg)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->image = sharedImage(ref, str, width, height, colorMap, inlineImg);
    if (maskColors)
        imageDraw->maskColors.assign(maskColors, maskColors + 2 * imageDraw->image->channels);

    imageDraw->transform << state->getCTM();
    drawLaiers.push_back(imageDraw);

    if (m_withSplashDraw && !inlineImg)
        SplashOutputDev::drawImage(state, ref, str, width, height, colorMap, interpolate, maskColors, inlineImg);
}

void ExternDrawOutputDivice::drawMaskedImage(GfxState *state, Object *ref, Stream *str, int width, int height, GfxImageColorMap *colorMap, GBool interpolate, Stream *maskStr, int maskWidth, int maskHeight, GBool maskInvert, GBool maskInterpolate)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->image = sharedImage(ref, str, width, height, colorMap);
    imageDraw->mask = sharedMask(ref, maskStr, maskWidth, maskHeight);
    imageDraw->setMaskInversion(maskInvert);
    imageDraw->transform << state->getCTM();
    drawLaiers.push_back(imageDraw);

    if (m_withSplashDraw)
        SplashOutputDev::drawMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskInvert, maskInterpolate);
}

void ExternDrawOutputDivice::drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str, int width, int height, GfxImageColorMap *colorMap, GBool interpolate, Stream *maskStr, int maskWidth, int maskHeight, GfxImageColorMap *maskColorMap, GBool maskInterpolate)
{
    auto imageDraw = std::make_shared<ImageDrawPrivate>();
    imageDraw->image = sharedImage(ref, str, width, height, colorMap);
    imageDraw->mask = sharedMask(ref, maskStr, maskWidth, maskHeight, maskColorMap);
    imageDraw->transform << state->getCTM();
    drawLaiers.push_back(imageDraw);
    if (m_withSplashDraw)
        SplashOutputDev::drawSoftMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskColorMap, maskInterpolate);
}
}
