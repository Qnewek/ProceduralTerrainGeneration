#include "utilities.h"
#include "math.h"

namespace utilities
{
    void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height) {
        for (int i = 0; i < width * height; ++i) {
            image[i] = static_cast<unsigned char>(data[i] * 255.0f);
        }
    }
	void SimpleMeshIndicies(unsigned int* indices, int width, int height) {
		int index = 0;
		for (int y = 0; y < height - 1; y++) {
			for (int x = 0; x < width - 1; x++) {
				indices[index++] = x + y * width;
				indices[index++] = x + (y + 1) * width;
				indices[index++] = x + 1 + y * width;

				indices[index++] = x + 1 + y * width;
				indices[index++] = x + (y + 1) * width;
				indices[index++] = x + 1 + (y + 1) * width;
			}
		}
    }
}


