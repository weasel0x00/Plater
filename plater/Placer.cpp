#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <random>
#include <chrono>
#include "Placer.h"
#include "log.h"

namespace Plater
{
    Placer::Placer(Request *request_)
        : solution(NULL),
        rotateDirection(0),
        request(request_)
    {
        for (auto part : request->quantities) {
            for (int i=0; i<part.second; i++) {
                PlacedPart *placedPart = new PlacedPart;
                placedPart->setPart(request->parts[part.first]);
                parts.push_back(placedPart);
            }
        }

        setGravityMode(PLACER_GRAVITY_YX);
    }

    Placer::~Placer()
    {
        if (myThread != NULL) {
            myThread->join();
            delete myThread;
        }
    }

    void Placer::sortParts(int sortType)
    {
        switch (sortType) {
            case PLACER_SORT_SURFACE_INC:
                sort(parts.begin(), parts.end(), [](const PlacedPart *a, const PlacedPart *b) {
                        return a->getSurface() > b->getSurface();
                        });
                break;
            case PLACER_SORT_SURFACE_DEC:
                sort(parts.begin(), parts.end(), [](const PlacedPart *a, const PlacedPart *b) {
                        return a->getSurface() < b->getSurface();
                        });
                break;
            default:
                unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
                shuffle(parts.begin(), parts.end(), std::default_random_engine(seed));
                break;
        }
    }

    PlacedPart *Placer::getNextPart()
    {
        PlacedPart *part = parts.back();
        parts.pop_back();

        return part;
    }
            
    void Placer::setRotateDirection(int direction)
    {
        rotateDirection = direction;
    }
            
    void Placer::setRotateOffset(int offset)
    {
        rotateOffset = offset;
    }
            
    void Placer::setGravityMode(int gravityMode)
    {
        switch (gravityMode) {
            case PLACER_GRAVITY_YX:
                xCoef = 1;
                yCoef = 10;
                break;
            case PLACER_GRAVITY_XY:
                xCoef = 10;
                yCoef = 1;
                break;
            case PLACER_GRAVITY_EQ:
                xCoef = 1;
                yCoef = 1;
                break;
        }
    }
    
    bool Placer::placePartSkyline(Plate *plate, PlacedPart *part)
    {
        std::string cacheName = part->getName();
        if (cache[plate][cacheName]) {
            return false;
        }

        const float precision = plate->precision;
        const int bw = plate->bmp->width;
        const int bh = plate->bmp->height;
        int dpix = (int)(request->delta / precision);
        if (dpix < 1) {
            dpix = 1;
        }
        const int rs = ceil(M_PI*2/request->deltaR);

        bool found = false;
        float betterScore = 0;
        int betterX = 0, betterY = 0, betterR = 0;
        std::vector<int> bottom, top;

        for (int r=(rotateDirection ? rs-1 : 0); rotateDirection ? r>=0 : r<rs; rotateDirection ? r-- : r++) {
            int vr = (r+rotateOffset)%rs;
            part->setRotation(vr);
            Bitmap *pb = part->getBmp();
            if (pb == NULL) {
                continue;
            }
            const int pw = pb->width;
            const int ph = pb->height;
            if (pw > bw || ph > bh) {
                continue;
            }

            // Per-column bottom/top of the part's footprint for this rotation.
            bottom.assign(pw, -1);
            top.assign(pw, -1);
            for (int px=0; px<pw; px++) {
                for (int py=0; py<ph; py++) {
                    if (pb->getPoint(px, py)) {
                        if (bottom[px] < 0) {
                            bottom[px] = py;
                        }
                        top[px] = py;
                    }
                }
            }

            const float gx0 = part->getGX();
            const float gy0 = part->getGY();

            for (int X=0; X + pw <= bw; X += dpix) {
                // "Drop" the part onto the skyline: the resting row is the
                // highest contact among the columns it covers. This is
                // overlap-free by construction (everything above colHeight is
                // empty), so no collision test is needed.
                int restingY = 0;
                for (int px=0; px<pw; px++) {
                    if (bottom[px] < 0) {
                        continue;
                    }
                    int need = plate->colHeight[X+px] - bottom[px];
                    if (need > restingY) {
                        restingY = need;
                    }
                }
                if (restingY + ph > bh) {
                    continue;
                }

                float gx = gx0 + X*precision;
                float gy = gy0 + restingY*precision;
                float score = gy*yCoef + gx*xCoef;
                if (!found || score < betterScore) {
                    found = true;
                    betterScore = score;
                    betterX = X;
                    betterY = restingY;
                    betterR = vr;
                }
            }
        }

        if (found) {
            part->setRotation(betterR);
            part->setOffset(betterX*precision, betterY*precision);
            plate->place(part);
            return true;
        } else {
            cache[plate][cacheName] = true;
            return false;
        }
    }

    bool Placer::placePart(Plate *plate, PlacedPart *part)
    {
        if (request->skyline && plate->mode == PLATE_MODE_RECTANGLE) {
            return placePartSkyline(plate, part);
        }

        std::string cacheName = part->getName();

        if (!cache[plate][cacheName]) {
            float betterX=0, betterY=0, betterScore;
            int betterR=0;
            int rs = ceil(M_PI*2/request->deltaR);
            bool found = false;

            for (int r=(rotateDirection ? rs-1 : 0); rotateDirection ? r>=0 : r<rs; rotateDirection ? r-- : r++) {
                int vr = (r+rotateOffset)%rs;
                part->setRotation(vr);
                if (part->getBmp() != NULL) {
                    for (float x=0; x<plate->width; x+=request->delta) {
                        for (float y=0; y<plate->height; y+=request->delta) {
                            float gx = part->getGX()+x;
                            float gy = part->getGY()+y;
                            float score = gy*yCoef+gx*xCoef;

                            if (!found || score < betterScore) {
                                part->setOffset(x, y);
                                if (plate->canPlace(part)) {
                                    found = true;
                                    betterX = x;
                                    betterY = y;
                                    betterScore = score;
                                    betterR = vr;
                                }
                            }
                        }
                    }
                }
            }
            if (found) {
                // _log("- Placing it @%g,%g r=%d\n", betterX, betterY, betterR);
                part->setRotation(betterR);
                part->setOffset(betterX, betterY);
                plate->place(part);
                return true;
            } else {
                cache[plate][cacheName] = true;
                return false;
            }
        } else {
            return false;
        }
    }

    Solution *Placer::place()
    {
        Solution *solution = new Solution(request->plateWidth, request->plateHeight, request->plateDiameter, request->plateMode, request->precision);
        solution->addPlate();

        _log("* Placer\n");
        while (parts.size()) {
            PlacedPart *part = getNextPart();
            // _log("- Trying to place %s...\n", part->getPart()->getFilename().c_str());
            bool placed = false;

            for (int i=0; i<solution->countPlates() && !placed; i++) {
                Plate *plate = solution->getPlate(i);

                if (placePart(plate, part)) {
                    placed = true;
                } else {
                    if (i+1 == solution->countPlates()) {
                        // _log("! Creating a new plate\n");
                        solution->addPlate();
                    }
                }
            }
        }

        _log("- Solution with %d plates\n", solution->countPlates());

        this->solution = solution;
        return solution;
    }
            
    void Placer::placeThreaded()
    {
        myThread = new std::thread([this](){
            this->place();
        });
    }
}
