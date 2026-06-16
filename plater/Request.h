#ifndef _PLATER_REQUEST_H
#define _PLATER_REQUEST_H

#include <map>
#include <sstream>
#include <iostream>
#include <vector>
#include "Part.h"
#include "Solution.h"
#include "PlacedPart.h"

// Output modes
#define REQUEST_STL 0
#define REQUEST_PPM 1
#define REQUEST_3MF 2

// Explore different sortings
#define REQUEST_SINGLE_SORT      0
#define REQUEST_MULTIPLE_SORTS   1

namespace Plater
{
    class Placer;

    class Request
    {
        public:
            Request();
            virtual ~Request();

            // Set the plate dimension
            void setPlateSize(float w, float h);

            // Read plater.conf
            void readFromStdin();
            void readFromFile(std::string filename);
            void readPartsFromString(std::string parts);
            void process();
            // Solve only (populate `solution`), without writing output files.
            void solve();
            // Run a set of placers concurrently and return the best solution.
            Solution *runPlacers(std::vector<Placer*> &placers);
            // Grow the plate from an ideal size toward the configured plate
            // size (the physical maximum -b), in `stepMm` increments, looking
            // for the smallest plate that fits the parts in `targetPlates`
            // plates. Width and height each start at their own ideal and grow
            // toward their own maximum. Writes the chosen solution's files.
            void processFit(double idealWmm, double idealHmm, double stepMm, int targetPlates);
            // Shrink fit: start at the full -b bed (which needs the fewest
            // plates) and step both bed dimensions down by `stepMm`, keeping the
            // smallest size that still packs into that baseline plate count. Stop
            // the first time a smaller size would need an extra plate (or no
            // longer fits a part) and keep the previous, larger size. Unlike
            // processFit it needs no ideal lower bound -- it descends on its own.
            void processShrink(double stepMm);

            // Plate mode (rectangular or circular)
            int plateMode;
            // Plates dimensions
            double plateWidth, plateHeight;
            double plateDiameter;
            // Number of random iterations
            int randomIterations;
            // Output mode (STL or PPM)
            int mode;
            // Sort mode (simple or multiple)
            int sortMode;
            // Precision 
            float precision;
            // Brute-force deltas
            float delta, deltaR;
            // Parts spacing
            float spacing;
            // Output file pattern
            std::string pattern;
            // Explicit output file name for the single 3MF file (-O). Empty
            // means derive the name from the pattern.
            std::string outputFile;

            void writeFiles(Solution *solution);
            void writeSTL(Plate *plate, const char *filename);
            void write3MF(Solution *solution, const char *filename);
            void writePpm(Plate *plate, const char *filename);
            void writePlatesInfo(Solution *solution);
        
            std::map<std::string, int> quantities;
            std::map<std::string, Part*> parts;
    
            // Request error
            bool hasError;
            std::string error;

            // Cancel the running request
            bool cancel;
            // Number of plates
            int plates;
            std::vector<std::string> generatedFiles;

            // Stats & solution
            int placersCount, placerCurrent;
            Solution *solution;

            // Enable the plates.csv file output
            bool platesInfo;

            // Number of threads
            unsigned int nbThreads;

            // Use the skyline (bottom-left drop) placement heuristic instead of
            // the full brute-force position grid (rectangular plates only).
            bool skyline;

            // Also try max-contact scored skyline placements (denser packing).
            // Only used together with skyline.
            bool contact;

            // Hole-aware pruned brute force: same placements as the default
            // brute force (fills holes/cavities) but with score-based pruning.
            bool prunedBrute;

            // Simulated-annealing search: instead of enumerating a fixed set of
            // sort orders, search over part orderings (and gravity/rotation
            // config), re-running the greedy placer on each candidate and
            // keeping the densest packing found. Quality-first, not speed-first.
            bool anneal;
            // Wall-clock budget for the annealing search, in seconds. Larger
            // budgets explore more orderings and pack tighter.
            double annealTime;

            // Balance pass for -A anneal: once the minimum plate count is found
            // (and it is >1), run a second annealing phase that, without using
            // more plates, evens out the total part area across the plates so
            // none is left sparse. Off by default (dense packing).
            bool balance;

            // Set by solveAnneal: whether the volume-balanced packing actually
            // fit in the minimum plate count at the last solved size. The shrink
            // search reads this so that, with -B, it only shrinks while the
            // plates can still be balanced (a tighter bed has no room to spread
            // the big parts). True when not balancing or balance isn't needed.
            bool balanceFit;

            // Search part orderings with simulated annealing and store the best
            // packing in `solution`. Seeded from the largest-first greedy result
            // so it never does worse than the brute-force algorithm.
            void solveAnneal();

            // Arrange the parts within `target` plates with the taller parts
            // pulled toward each plate's centre and balanced across plates (the
            // -T layout). Returns the chosen solution, or NULL if no centred
            // layout fits in `target` plates. Used by the non-anneal -T path.
            Solution *tallCenterWithin(int target);

            // Post-pass: try to empty the sparsest plate by re-placing its
            // parts into the gaps of the others; kept only if plates drop.
            bool consolidate;

            // Place taller parts toward the centre of the plate (-T; helps print
            // reliability). With -A anneal the search itself scores each
            // placement toward the centre (and composes with -B); otherwise a
            // post-pass centres the tall parts within the minimum plate count
            // (see tallCenterWithin).
            bool tallCenter;

            // Try to reduce the plate count of `solution` in place. Safe: only
            // applied when it strictly reduces the number of plates.
            void consolidateSolution();

        protected:
            void addPart(std::string filename, int quantity, std::string orientation);
            std::vector<std::string> getChunks(string line);
            void readParts();
            std::string readLine();
            std::istream *stream;
            // The raw parts specification, kept so the parts can be reloaded
            // at different plate sizes during processFit().
            std::string partsText;
    };
}

#endif
