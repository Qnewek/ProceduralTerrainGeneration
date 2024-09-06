#include "utilities.h"
#include "math.h"

namespace utilities
{
    void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height) {
        for (int i = 0; i < width * height; ++i) {
            image[i] = static_cast<unsigned char>(data[i] * 255.0f);
        }
    }
}


