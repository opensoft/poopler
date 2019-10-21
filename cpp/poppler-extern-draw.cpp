#include "poppler-extern-draw.h"

namespace poppler {
// Transform a point from user space to device space. Multiplication point on matrix
Point operator*(const Point &p, const TransformationMatrix &matrix)
{
    return {p[0] * matrix[0] + p[1] * matrix[2] + matrix[4], p[0] * matrix[1] + p[1] * matrix[3] + matrix[5]};
}
//result = a * b;
TransformationMatrix operator*(const TransformationMatrix &a, const TransformationMatrix &b)
{
    return {a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],        a[2] * b[0] + a[3] * b[2],
            a[2] * b[1] + a[3] * b[3], a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]};
}
//like multiplication matrix on shift matrix:
//          [1 0 0]
// matrix * [0 1 0]
//          [x y 1]
TransformationMatrix operator+(const TransformationMatrix &matrix, const Point &p)
{
    return {
        matrix[0], matrix[1], matrix[2], matrix[3], matrix[4] + p[0], matrix[5] + p[1],
    };
}

//like multiplication shift matrix on matrix:
//[1 0 0]
//[0 1 0] * matrix
//[x y 1]
TransformationMatrix operator+(const Point &p, const TransformationMatrix &m)
{
    return {
        m[0], m[1], m[2], m[3], m[4] + p[0] * m[0] + p[1] * m[2], m[5] + p[0] * m[1] + p[1] * m[3],
    };
}

ProcessStep::~ProcessStep()
{}

ProcessStep::StepInformation StateSave::stepInformation()
{
    return ProcessStep::StepInformation::StateSave;
}

ProcessStep::StepInformation StateRestore::stepInformation()
{
    return ProcessStep::StepInformation::StateRestore;
}

ProcessStep::StepInformation TextStart::stepInformation()
{
    return ProcessStep::StepInformation::TextStart;
}

ProcessStep::StepInformation TextStop::stepInformation()
{
    return ProcessStep::StepInformation::TextStop;
}

ProcessStep::StepInformation ContourDraw::stepInformation()
{
    return ProcessStep::StepInformation::ContourDraw;
}

ProcessStep::StepInformation GlyphDraw::stepInformation()
{
    return ProcessStep::StepInformation::GlyphDraw;
}

ProcessStep::StepInformation ImageDraw::stepInformation()
{
    return ProcessStep::StepInformation::ImageDraw;
}

ProcessStep::StepInformation TransparencyGroupStart::stepInformation()
{
    return ProcessStep::StepInformation::TransparencyGroupStart;
}

poppler::ProcessStep::StepInformation TransparencyGroupStop::stepInformation()
{
    return ProcessStep::StepInformation::TransparencyGroupStop;
}

poppler::ProcessStep::StepInformation TransparencyGroupDraw::stepInformation()
{
    return ProcessStep::StepInformation::TransparencyGroupDraw;
}
} // namespace poppler
