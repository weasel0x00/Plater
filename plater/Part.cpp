#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <iostream>
#include "Part.h"

using namespace std;

namespace Plater
{
    Part::Part()
        : bmp(NULL), allBmp(NULL)
    {
    }

    Part::~Part()
    {
        // allBmp owns the bitmaps; bmp only holds (aliased) views into it.
        if (allBmp != NULL) {
            for (int i=0; i<bmps; i++) {
                if (allBmp[i] != NULL) {
                    delete allBmp[i];
                }
            }
            delete[] allBmp;
        }
        if (bmp != NULL) {
            delete[] bmp;
        }
    }

    int Part::load(std::string filename_, float precision_, float deltaR_, float spacing,
            string orientation, float plateWidth, float plateHeight)
    {
        precision = precision_;
        deltaR = deltaR_;
        bmps = ceil((M_PI*2)/deltaR);
        filename = filename_;

        // Size-independent geometry work (disk read, pixelize, rotate, trim):
        // done once. allBmp keeps every rotation; the per-plate-size filtering
        // lives in applyPlateSize so a fit search can re-filter cheaply.
        model = loadModelFromFile(filename.c_str());

        // Validate the mesh before any geometry work. An empty model means the
        // file wasn't a readable STL (the loaders return an empty Model rather
        // than throwing on garbage); non-finite coordinates (NaN/Inf) would flow
        // into pixelize() and produce a bogus Bitmap size -> a huge or undefined
        // allocation. Reject both up front with a clear error.
        size_t nFaces = 0;
        for (auto &vol : model.volumes) {
            for (auto &face : vol.faces) {
                for (int i=0; i<3; i++) {
                    const Point3 &p = face.v[i];
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                        throw string("Malformed STL (non-finite coordinate): " + filename);
                    }
                }
                nFaces++;
            }
        }
        if (nFaces == 0) {
            throw string("Empty or unreadable STL (no triangles): " + filename);
        }

        model = model.putFaceOnPlate(orientation);
        // Mesh volume is orientation-independent; compute it once for plate
        // balancing (a print-time proxy).
        volume = model.getVolume();
        allBmp = new Bitmap*[bmps];
        bmp = new Bitmap*[bmps];
        allBmp[0] = model.pixelize(precision, spacing);

        Point3 minP = model.min();
        Point3 maxP = model.max();
        width = maxP.x-minP.x + 2*spacing;
        height = maxP.y-minP.y + 2*spacing;
        zHeight = maxP.z-minP.z;   // print height, in the chosen orientation

        for (int k=1; k<bmps; k++) {
            Bitmap *rotated = Bitmap::rotate(allBmp[0], k*deltaR);
            allBmp[k] = Bitmap::trim(rotated);
            delete rotated;
        }

        return applyPlateSize(plateWidth, plateHeight);
    }

    int Part::applyPlateSize(float plateWidth, float plateHeight)
    {
        surface = 0;
        int correct = 0;

        for (int k=0; k<bmps; k++) {
            // Will this rotation fit on the plate ?
            if (allBmp[k]->width*precision < plateWidth && allBmp[k]->height*precision < plateHeight) {
                bmp[k] = allBmp[k];
                surface += allBmp[k]->width * allBmp[k]->height;
                correct++;
            } else {
                bmp[k] = NULL;
            }
        }

        if (correct > 0) {
            surface /= (float)correct;
        }
        return correct;
    }
            
    float Part::getSurface() const
    {
        return surface;
    }

    float Part::getVolume() const
    {
        return volume;
    }

    std::string Part::getFilename()
    {
        return filename;
    }
            
    Bitmap *Part::getBmp(int index) const
    {
        return bmp[index];
    }
            
    float Part::getDensity(int index) const
    {
        Bitmap *bmp = getBmp(index);
        return bmp->pixels/(bmp->width*bmp->height);
    }
}
