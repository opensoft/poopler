#ifndef POPPLER_EXTERN_DRAW_H
#define POPPLER_EXTERN_DRAW_H

#include "poppler-global.h"

#include <memory>
#include <array>
#include <list>
#include <vector>

namespace poppler {

// [ m[0] m[1] 0 ]
// [ m[2] m[3] 0 ]
// [ m[4] m[5] 1 ]
using TransformationMatrix = std::array<double, 6>;

// x = m[0]  y = m[1]. Point in PDF is one row vector (matrix 1x3) [x y 1]
using Point = std::array<double, 2>;

// Transform a point from user space to device space. Multiplication point on matrix
POPPLER_CPP_EXPORT Point operator*(const Point &p, const TransformationMatrix &matrix);

//result = a * b;
POPPLER_CPP_EXPORT TransformationMatrix operator*(const TransformationMatrix &a, const TransformationMatrix &b);

//like multiplication matrix on shift matrix:
//          [1 0 0]
// matrix * [0 1 0]
//          [x y 1]
POPPLER_CPP_EXPORT TransformationMatrix operator+(const TransformationMatrix &matrix, const Point &p);

//like multiplication shift matrix on matrix:
//[1 0 0]          
//[0 1 0] * matrix
//[x y 1]          
POPPLER_CPP_EXPORT TransformationMatrix operator+(const Point &p, const TransformationMatrix &m);

struct Color
{
    std::vector<float> components;
    enum Format
    {
        //Device    Color Spaces
        DeviceGray,
        DeviceRGB,
        DeviceCMYK,
        //CIE-Based Color Spaces
        CalGray,
        CalRGB,
        Lab,
        ICCBased,

        Indexed,
        Separation,
        DeviceN,

        Pattern
    } format;
};

struct ImageData
{
    Color::Format format;
    int bitsPerChannel;
    int channels;
    int width;
    int height;
    int rowSize;
    //Each n-bit unit within the bit stream is interpreted as an unsigned integer
    //in the range 0 to 2n-1, with the high-order bit first
    // So, PDF use big-endian bit order. It is awful, because
    // in mask value 128 - set first pixel to 1 and mask 1 - set eighth pixel (less bit set high pixel)
    // I hope that after decoding byte-order is set to right order.
    std::vector<unsigned char> data;
};

struct ContourData
{
    struct ContourPoint
    {
        double x, y;
        // first point on each subpath sets this flag
        bool firstPoint;// = 0x01,
        // last point on each subpath sets this flag
        bool lastPoint;// = 0x02,
        // if the subpath is closed, its first and last points must be
        // identical, and must set this flag
        bool pathClosed;// = 0x04,
        // curve control points set this flag
        bool pathCurve;// = 0x08,
    };
    std::vector<ContourPoint> points;
};

using SharedContourData = std::shared_ptr<ContourData>;

class ProcessStep
{
public:
    virtual ~ProcessStep()
    {
    };
    enum StepInformation
    {
        ContourDraw,
        ImageDraw,
        StateSave,
        StateRestore,
        TextStart,
        GlyphDraw,
        TextStop,
        //extra codes for user steps : >= Extra
        Extra
    };
    virtual StepInformation stepInformation() = 0;
};

struct StateSave : public ProcessStep
{
    StepInformation stepInformation() override
    {
        return ProcessStep::StateSave;
    }
};

struct StateRestore : public ProcessStep
{
    StepInformation stepInformation() override
    {
        return ProcessStep::StateRestore;
    }
};

struct TextStart : public ProcessStep
{
    StepInformation stepInformation() override
    {
        return ProcessStep::TextStart;
    }
};

struct TextStop : public ProcessStep
{
    StepInformation stepInformation() override
    {
        return ProcessStep::TextStop;
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
        return ProcessStep::ContourDraw;
    }
    //affects only if set
    Color fillColor;
    // Even-Odd Rule must used for fill the contour or otherwise Nonzero Winding Number Rule
    bool evenOddFillRule = false;
    Color strokeColor;
    double lineWidth;
    SharedContourData contourData;
};

struct GlyphDraw : public ContourDraw
{
    StepInformation stepInformation() override
    {
        return ProcessStep::GlyphDraw;
    }
    //if this contour should to be accumulate to current clip state
    // splash clip deleted after StateRestore. Is it right? Could clip 
    // accumulation continue on same level of the state?
    bool clip;
    unsigned int unicode;
};

struct ImageDraw : public DrawStep
{
    StepInformation stepInformation() override
    {
        return ProcessStep::ImageDraw;
    }
    std::shared_ptr<ImageData> image;
    std::shared_ptr<ImageData> mask;
    //1. if mask empty and maskColors is not - then maskColors contain min and max pairs for each
    // color component of pixel which must be excluded form draw (chromokey?) 
    //2. if mask isn't empty maskColors used for set inversion of mask: if maskColors[0] > maskColors[1]
    std::vector<int> maskColors;
    // When image is empty this color used for drawing mask.
    // We can't use image (container) in this case because
    // mask 1 (0 if inverse) in pixel is transparent - not drawing (image pixels all draw)
    Color fillColor;
};

using ProcessStepStore = std::list<std::shared_ptr<ProcessStep>>;
}

#endif // POPPLER_EXTERN_DRAW_H
