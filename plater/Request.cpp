#define _USE_MATH_DEFINES
#include <set>
#include <algorithm>
#include <chrono>
#include <random>
#include <functional>
#include <thread>
#include <math.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include "util.h"
#include "Request.h"
#include "Placer.h"
#include "Plate.h"
#include "Solution.h"
#include "ThreeMF.h"
#include "log.h"
#include "sleep.h"

using namespace std;

#if defined(_WIN32) || defined(_WIN64)
#define snprintf _snprintf
#endif

namespace Plater
{
    Request::Request()
        : 
        plateMode(PLATE_MODE_RECTANGLE),
        plateWidth(150000),
        plateHeight(150000),
        randomIterations(3),
        mode(REQUEST_STL),
        sortMode(REQUEST_SINGLE_SORT),
        precision(500),
        delta(1000),
        deltaR(M_PI/2),
        spacing(1500),
        pattern("plate_%03d"),
        cancel(false),
        solution(NULL),
        nbThreads(1),
        platesInfo(false),
        skyline(false),
        contact(false),
        prunedBrute(false),
        consolidate(false),
        tallCenter(false),
        anneal(false),
        annealTime(30.0),
        balance(false)
    {
    }

    Request::~Request()
    {
        for (auto part : parts) {
            delete part.second;
        }

        if (solution != NULL) {
            delete solution;
        }
    }
            
    std::string Request::readLine()
    {
        char buffer[4096];
        stream->getline(buffer, 4096);

        return string(buffer);
    }

    void Request::setPlateSize(float w, float h)
    {
        plateWidth = w*1000;
        plateHeight = h*1000;
    }

    void Request::addPart(std::string filename, int quantity, string orientation)
    {
        if (!cancel && !hasError) {
            if (filename != "" && quantity != 0) {
                _log("- Loading %s (quantity %d, orientation %s)...\n", filename.c_str(), quantity, orientation.c_str());
                parts[filename] = new Part;
                int loaded = 
                    parts[filename]->load(filename, precision, deltaR, spacing, orientation, plateWidth, plateHeight);
                quantities[filename] = quantity;

                if (loaded == 0) {
                    ostringstream oss;
                    oss << "Part " << filename << " is too big for the plate ";
                    oss << " (bed too small? try more angles?)";
                    error = oss.str();
                    hasError = true;
                }
            }
        }
    }
            
    void Request::readPartsFromString(std::string parts)
    {
        partsText = parts;
        istringstream s(parts);

        stream = &s;
        readParts();
    }

    /**
     * Read chunks from line, could be:
     *
     * file.stl quantity
     *      or
     * file.stl quantity orientation
     */
    vector<string> Request::getChunks(string line)
    {
        vector<string> parts = split(line, ' ');
        vector<string> chunks;
        int n = parts.size();
        string filename;

        if (n < 1) {
            return parts;
        }

        // XXX: This is not really clean, something smarter could 
        // be done here

        int quantity;
        for (quantity=n-1; quantity>0; quantity--) {
            if (isNumeric(parts[quantity])) {
                break;
            }
        }
        for (int i=0; i<quantity; i++) {
            if (filename != "") {
                filename += " ";
            }
            filename += parts[i];
        }
        chunks.push_back(filename);
        for (int i=quantity; i<n; i++) {
            chunks.push_back(parts[i]);
        }

        return chunks;
    }

    void Request::readParts()
    {
        parts.clear();
        quantities.clear();

        hasError = false;
        while (!stream->eof()) {
            string line = readLine();
            if (line[0] != '#') {
                line = trim(line);
                vector<string> chunks = getChunks(line);
                if (chunks.size() > 0) {
                    string filename = chunks[0];
                    int quantity = 1;
                    string orientation = "bottom";

                    if (chunks.size() >= 2) quantity = atof(chunks[1].c_str());

                    if (chunks.size() >= 3) {
                        orientation = chunks[2];
                    }

                    try {
                        addPart(filename, quantity, orientation);
                    } catch (string error_) {
                        hasError = true;
                        error = error_;
                        return;
                    }
                }
            }
        }
    }
            
    void Request::readFromFile(std::string filename)
    {
        if (!chdirFile(filename)) {
            cerr << "! Can't go to the directory of " << filename << endl;
        }
        ifstream ifile(getBasename(filename));

        if (!ifile) {
            cerr << "! Can't open configuration file " << filename << endl;
        } else {
            cerr << "* Reading from " << filename << endl;
            // Read the whole file so the parts can be reloaded later (e.g.
            // processFit reloads at different plate sizes).
            stringstream buffer;
            buffer << ifile.rdbuf();
            readPartsFromString(buffer.str());
        }
    }

    void Request::readFromStdin()
    {
        _log("* Reading request from stdin\n");
        stringstream buffer;
        buffer << cin.rdbuf();
        readPartsFromString(buffer.str());
    }
    
    void Request::writeSTL(Plate *plate, const char *filename)
    {
        Model model = plate->createModel();

        try {
            saveModelToFileBinary(filename, &model);
        } catch (string error_) {
            hasError = true;
            error = error_;
        }
    }

    void Request::write3MF(Solution *solution, const char *filename)
    {
        // Plate (bed) dimensions in mm; for a circular bed use the diameter
        // as the bounding square so the slicer grid still lines up.
        double w = (plateMode == PLATE_MODE_CIRCLE ? plateDiameter : plateWidth) / 1000.0;
        double d = (plateMode == PLATE_MODE_CIRCLE ? plateDiameter : plateHeight) / 1000.0;

        if (!saveSolutionTo3MF(filename, solution, w, d)) {
            ostringstream oss;
            oss << "Can't write to " << filename;
            error = oss.str();
            hasError = true;
            logError("Error: can't write to %s\n", filename);
        }
    }

    void Request::writePpm(Plate *plate, const char *filename)
    {
        ofstream ofile(filename);
        if (ofile) {
            ofile << plate->bmp->toPpm();
            ofile.close();
        } else {
            ostringstream oss;
            oss << "Can't write to " << filename;
            error = oss.str();
            hasError = true;
            logError("Error: can't write to %s\n", filename);
        }
    }

    void Request::writePlatesInfo(Solution *solution)
    {
        std::ofstream ofs("plates.csv");

        ofs << "plate,part,posX,posY,rotation" << std::endl;

        for (int i=0; i<solution->countPlates(); i++) {
            Plate *plate = solution->getPlate(i);

            for (auto part : plate->parts) {
                ofs << i+1 << ",";
                ofs << part->getName() << ",";
                ofs << part->getCenterX()/1000.0 << ",";
                ofs << part->getCenterY()/1000.0 << ",";
                ofs << (part->getRotation()*part->getPart()->deltaR*180.0/M_PI) << "";
                ofs << std::endl;

            }
        }

        ofs.close();
    }

    void Request::writeFiles(Solution *solution)
    {
        generatedFiles.clear();

        _log("* Exporting\n");

        // Writing plates info
        if (platesInfo) {
            _log("- Exporting plates.csv...\n");
            writePlatesInfo(solution);
        }

        // 3MF packs every plate into a single file rather than one file
        // per plate, so it is handled on its own.
        if (mode == REQUEST_3MF) {
            string out;
            if (!outputFile.empty()) {
                // Explicit output name (-O); add the extension if missing.
                out = outputFile;
                if (out.size() < 4 || out.substr(out.size()-4) != ".3mf") {
                    out += ".3mf";
                }
            } else {
                out = pattern;
                // Drop any printf-style placeholder (and trailing separators)
                // since there is only one output file.
                size_t pos = out.find('%');
                if (pos != string::npos) {
                    out = out.substr(0, pos);
                }
                while (!out.empty() &&
                       (out.back() == '_' || out.back() == '-' ||
                        out.back() == '.' || out.back() == ' ')) {
                    out.pop_back();
                }
                if (out.empty()) {
                    out = "plates";
                }
                out += ".3mf";
            }

            _log("- Exporting %s...\n", out.c_str());
            generatedFiles.push_back(out);
            write3MF(solution, out.c_str());
            return;
        }

        switch (mode) {
            case REQUEST_PPM:
                pattern += ".ppm";
                break;
            case REQUEST_STL:
                pattern += ".stl";
                break;
        }

        // Exporting each file
        char *buffer = new char[pattern.size()+64];
        for (int i=0; i<solution->countPlates(); i++) {
            Plate *plate = solution->getPlate(i);
            int plateNumber = i+1;
            snprintf(buffer, pattern.size()+63, pattern.c_str(), plateNumber);
            _log("- Exporting %s...\n", buffer);
            generatedFiles.push_back(string(buffer));

            switch (mode) {
                case REQUEST_STL:
                    writeSTL(plate, buffer);
                    break;
                case REQUEST_PPM:
                    writePpm(plate, buffer);
                    break;
            }
        }
        delete[] buffer;
    }

    void Request::solve()
    {
        if (solution != NULL) {
            Solution *toDelete = solution;
            solution = NULL;
            delete toDelete;
        }

        if (cancel || hasError) {
            return;
        }

        if (plateMode == PLATE_MODE_RECTANGLE) {
            _log("- Plate size: %g x %g microm\n", plateWidth, plateHeight);
        } else {
            _log("- Plate size: %g microm (circle)\n", plateDiameter);
        }

        if (anneal) {
            solveAnneal();
            if (!cancel && solution != NULL) {
                plates = solution->countPlates();
            }
            return;
        }

        // Build the normal (dense) placers and find the minimum plate count.
        vector<Placer*> placers;
        int lastSort;
        if (sortMode == REQUEST_SINGLE_SORT) {
            lastSort = PLACER_SORT_SURFACE_DEC;
        } else {
            lastSort = PLACER_SORT_SHUFFLE+randomIterations;
        }
        for (int sortMode=0; sortMode<=lastSort; sortMode++) {
            for (int rotateOffset=0; rotateOffset<2; rotateOffset++) {
                for (int rotateDirection=0; rotateDirection<2; rotateDirection++) {
                    for (int gravity=0; gravity<PLACER_GRAVITY_EQ; gravity++) {
                        Placer *placer = new Placer(this);
                        placer->sortParts(sortMode);
                        placer->setGravityMode(gravity);
                        placer->setRotateDirection(rotateDirection);
                        placer->setRotateOffset(rotateOffset);
                        placers.push_back(placer);
                    }

                    // Add a max-contact scored skyline placer for this config.
                    // The solver keeps the best result, so these only ever help.
                    if (skyline && contact) {
                        Placer *placer = new Placer(this);
                        placer->sortParts(sortMode);
                        placer->setGravityMode(PLACER_GRAVITY_YX);
                        placer->setRotateDirection(rotateDirection);
                        placer->setRotateOffset(rotateOffset);
                        placer->setScoreMode(PLACER_SCORE_CONTACT);
                        placers.push_back(placer);
                    }
                }
            }
        }

        Solution *dense = runPlacers(placers);

        if (tallCenter && dense != NULL) {
            // Keep the dense plate count, but try to centre the tall parts
            // within it. Accept it only if it fits the same number of plates --
            // fewer plates always beats centring.
            int target = dense->countPlates();
            Solution *chosen = tallCenterWithin(target);
            if (chosen != NULL) {
                solution = chosen;
                delete dense;
            } else {
                _log("- Keeping dense %d-plate packing\n", target);
                solution = dense;
            }
        } else {
            solution = dense;
        }

        if (!cancel && solution != NULL) {
            plates = solution->countPlates();
        }
    }

    Solution *Request::tallCenterWithin(int target)
    {
        // Prefer centred-and-balanced; if that needs more plates, fall back to
        // dense-but-balanced (tall parts still spread across plates). Returns
        // NULL if neither fits in `target` plates (caller keeps its packing).
        const bool tryCenter[2] = { true, false };
        for (int t=0; t<2 && !cancel; t++) {
            for (int ro=0; ro<2 && !cancel; ro++) {
                for (int rd=0; rd<2 && !cancel; rd++) {
                    Placer pc(this);
                    pc.sortParts(PLACER_SORT_HEIGHT_DEC);
                    pc.setRotateOffset(ro);
                    pc.setRotateDirection(rd);
                    Solution *c = pc.placeCenterBalanced(target, tryCenter[t]);
                    if (c != NULL && c->countPlates() <= target) {
                        _log("- Tall parts %s within %d plate(s)\n",
                                tryCenter[t] ? "centred & balanced" : "balanced", target);
                        return c;
                    } else if (c != NULL) {
                        delete c;
                    }
                }
            }
        }
        return NULL;
    }

    void Request::solveAnneal()
    {
        // Canonical instance list, expanded in the SAME order Placer's
        // constructor walks request->quantities. A permutation of [0,N) then
        // maps 1:1 onto a placer's part queue via Placer::setOrder().
        std::vector<Part*> insts;
        for (auto &q : quantities) {
            for (int i=0; i<q.second; i++) {
                insts.push_back(parts[q.first]);
            }
        }
        const int N = (int)insts.size();
        if (N == 0) {
            solution = NULL;
            return;
        }

        // A search state is an ordering plus the discrete placement config the
        // greedy already understands (gravity bias + rotation enumeration).
        struct Config { int gravity, rotOffset, rotDir; };

        // Evaluate a candidate ordering with the existing greedy placer. `cap`
        // bounds the plate count (0 = uncapped); a capped run that doesn't fit
        // returns NULL.
        auto evaluate = [&](const std::vector<int> &order, const Config &c,
                            int cap) -> Solution* {
            Placer *p = new Placer(this);
            p->setOrder(order);
            p->setGravityMode(c.gravity);
            p->setRotateOffset(c.rotOffset);
            p->setRotateDirection(c.rotDir);
            if (cap > 0) {
                p->setMaxPlates(cap);
            }
            Solution *s = p->place();   // NULL only when capped and it won't fit
            delete p;                   // queue consumed into `s`
            return s;
        };

        // Per-plate "load" = the sum of its parts' surface areas. The balancing
        // phase drives these toward equality. Returned as a coefficient of
        // variation: 0 == perfectly balanced, scale-free across part sets.
        auto imbalance = [&](Solution *s) -> double {
            int n = s->countPlates();
            if (n < 2) {
                return 0.0;
            }
            std::vector<double> area(n, 0.0);
            double mean = 0;
            for (int i=0; i<n; i++) {
                for (auto pp : s->getPlate(i)->parts) {
                    area[i] += pp->getSurface();
                }
                mean += area[i];
            }
            mean /= n;
            if (mean <= 0) {
                return 0.0;
            }
            double var = 0;
            for (double a : area) {
                var += (a-mean)*(a-mean);
            }
            return sqrt(var/n) / mean;
        };

        // Result of one annealing chain: the best packing it found plus the
        // ordering/config that produced it (for the next phase to reuse).
        struct ChainResult {
            Solution *sol;
            std::vector<int> order;
            Config config;
            float score;
            long iters;
        };

        // One simulated-annealing chain with its own RNG (seeded from `seed`, so
        // parallel chains explore different trajectories). Minimises `objective`
        // (lower is better), keeping the single best Solution seen. Returns a
        // NULL sol only when the seed itself is infeasible under `cap`.
        auto runChain = [&](unsigned seed, std::function<float(Solution*)> objective,
                            int cap, std::vector<int> curOrder, Config curConfig,
                            double budget, double T0, double Tmin,
                            bool logProgress, const char *label) -> ChainResult
        {
            std::mt19937 rng(seed);
            std::uniform_real_distribution<double> unit(0.0, 1.0);
            auto randIdx = [&](int n){ return (int)(unit(rng) * n) % n; };

            Solution *best = evaluate(curOrder, curConfig, cap);
            if (best == NULL) {
                return ChainResult{NULL, curOrder, curConfig, 0.0f, 0};
            }
            float bestScore = objective(best);
            float curScore = bestScore;
            std::vector<int> bestOrder = curOrder;
            Config bestConfig = curConfig;
            long iters = 0;

            // Geometric cooling over the wall-clock budget: early moves explore
            // freely, late moves behave near-greedily. (Nothing to permute or no
            // budget -> just return the seed.)
            if (N >= 2 && budget > 0) {
                auto startTime = std::chrono::steady_clock::now();
                while (!cancel) {
                    double elapsed = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - startTime).count();
                    if (elapsed >= budget) {
                        break;
                    }
                    double T = T0 * pow(Tmin/T0, elapsed/budget);

                    // Propose a neighbour: perturb the order, or nudge the config.
                    std::vector<int> nbOrder = curOrder;
                    Config nbConfig = curConfig;
                    int move = randIdx(4);
                    if (move == 3) {
                        switch (randIdx(3)) {
                            case 0: nbConfig.gravity   = randIdx(PLACER_GRAVITY_EQ+1); break;
                            case 1: nbConfig.rotOffset = randIdx(2); break;
                            case 2: nbConfig.rotDir    = randIdx(2); break;
                        }
                    } else {
                        int i = randIdx(N), j = randIdx(N);
                        while (j == i) {
                            j = randIdx(N);
                        }
                        if (move == 0) {                 // swap two positions
                            std::swap(nbOrder[i], nbOrder[j]);
                        } else if (move == 1) {          // move one part to a new slot
                            int v = nbOrder[i];
                            nbOrder.erase(nbOrder.begin()+i);
                            nbOrder.insert(nbOrder.begin()+j, v);
                        } else {                          // reverse a segment
                            if (i > j) std::swap(i, j);
                            std::reverse(nbOrder.begin()+i, nbOrder.begin()+j+1);
                        }
                    }

                    Solution *cand = evaluate(nbOrder, nbConfig, cap);
                    iters++;
                    if (cand == NULL) {
                        continue;   // infeasible under the cap: reject the move
                    }
                    float candScore = objective(cand);

                    // Keep the single best packing this chain has seen.
                    if (candScore < bestScore) {
                        delete best;
                        best = cand;
                        bestScore = candScore;
                        bestOrder = nbOrder;
                        bestConfig = nbConfig;
                        if (logProgress) {
                            _log("- [%s] new best: %d plates, objective %g @%.1fs\n",
                                    label, best->countPlates(), bestScore, elapsed);
                        }
                    } else {
                        delete cand;   // not the best; only its order/score lives on
                    }

                    // Metropolis acceptance drives the random walk's current state.
                    float delta = candScore - curScore;
                    if (delta <= 0 || unit(rng) < exp(-delta/T)) {
                        curOrder.swap(nbOrder);
                        curConfig = nbConfig;
                        curScore = candScore;
                    }
                }
            }
            return ChainResult{best, bestOrder, bestConfig, bestScore, iters};
        };

        // Run `nbThreads` independent chains concurrently and return the best
        // result (freeing the rest). Each evaluate() builds its own Placer over
        // read-only Part bitmaps, so concurrent placement is safe.
        auto runParallel = [&](std::function<float(Solution*)> objective, int cap,
                               std::vector<int> seedOrder, Config seedConfig,
                               double budget, double T0, double Tmin,
                               const char *label) -> ChainResult
        {
            int chains = (nbThreads < 1) ? 1 : (int)nbThreads;
            std::vector<ChainResult> results(chains);
            if (chains == 1) {
                results[0] = runChain(0x9e3779b9u, objective, cap, seedOrder,
                        seedConfig, budget, T0, Tmin, true, label);
            } else {
                std::vector<std::thread> threads;
                for (int c=0; c<chains; c++) {
                    threads.emplace_back([&,c]() {
                        results[c] = runChain(0x9e3779b9u + 0x100u*(unsigned)c,
                                objective, cap, seedOrder, seedConfig,
                                budget, T0, Tmin, false, label);
                    });
                }
                for (auto &th : threads) {
                    th.join();
                }
            }

            // Keep the lowest-objective feasible chain; free the others.
            int bestIdx = -1;
            long totIters = 0;
            for (int c=0; c<chains; c++) {
                totIters += results[c].iters;
                if (results[c].sol == NULL) {
                    continue;
                }
                if (bestIdx < 0 || results[c].score < results[bestIdx].score) {
                    bestIdx = c;
                }
            }
            if (bestIdx < 0) {
                _log("- [%s] no feasible packing in %d chain(s)\n", label, chains);
                return ChainResult{NULL, seedOrder, seedConfig, 0.0f, totIters};
            }
            for (int c=0; c<chains; c++) {
                if (c != bestIdx && results[c].sol != NULL) {
                    delete results[c].sol;
                }
            }
            _log("- [%s] %d chain(s), %ld iterations, best %d plates (objective %g)\n",
                    label, chains, totIters, results[bestIdx].sol->countPlates(),
                    results[bestIdx].score);
            return results[bestIdx];
        };

        // Seed largest-surface-first (the proven greedy default) so annealing
        // starts from at-least-brute quality and can only improve on it.
        std::vector<int> seedOrder(N);
        for (int i=0; i<N; i++) {
            seedOrder[i] = i;
        }
        std::stable_sort(seedOrder.begin(), seedOrder.end(), [&](int a, int b){
            return insts[a]->getSurface() > insts[b]->getSurface();
        });
        Config seedConfig{PLACER_GRAVITY_YX, 0, 0};

        int chains = (nbThreads < 1) ? 1 : (int)nbThreads;
        _log("* Annealing (%d parts, %.0fs budget, %d chain(s)%s%s)\n",
                N, annealTime, chains,
                (balance && !tallCenter) ? ", +balance" : "",
                tallCenter ? ", +tall-centre" : "");

        // Phase 1: minimise the plate count. score() also empties the trailing
        // plate, which is what lets a plate be dropped entirely.
        ChainResult r1 = runParallel([](Solution *s){ return s->score(); }, 0,
                seedOrder, seedConfig, annealTime, 1.0, 0.01, "min-plates");
        Solution *result = r1.sol;

        // Phase 2 (optional, -B): with the plate count fixed at the phase-1
        // minimum, even out the surface area across plates so none is left
        // sparse. The cap forbids using more plates; a large bonus still rewards
        // ever using fewer (dropping a plate always beats balancing). Skipped
        // under -T, which re-derives the whole layout (and already spreads the
        // tall parts across plates).
        if (balance && !tallCenter && result != NULL && result->countPlates() > 1) {
            int P = result->countPlates();
            auto balanceObj = [&,P](Solution *s) -> float {
                double v = imbalance(s);
                if (s->countPlates() < P) {
                    v -= 1000.0;
                }
                return (float)v;
            };
            ChainResult r2 = runParallel(balanceObj, P, r1.order, r1.config,
                    annealTime, 0.2, 0.002, "balance");
            if (r2.sol != NULL) {
                _log("- Balanced %d plates: area spread %.1f%% -> %.1f%% (CoV)\n",
                        P, 100.0*imbalance(result), 100.0*imbalance(r2.sol));
                delete result;
                result = r2.sol;
            }
        } else if (balance && tallCenter) {
            _log("- Note: -T re-arranges within the minimum plates; -B skipped\n");
        }

        // Phase T (optional, -T): centre the taller parts within the final plate
        // count, reusing the same logic as the non-anneal path. Anneal's role
        // here is to have found the minimum plate count to centre within.
        if (tallCenter && result != NULL) {
            int target = result->countPlates();
            Solution *tc = tallCenterWithin(target);
            if (tc != NULL) {
                delete result;
                result = tc;
            } else {
                _log("- Keeping anneal %d-plate packing (no centred layout fit)\n",
                        target);
            }
        }

        solution = result;
    }

    Solution *Request::runPlacers(std::vector<Placer*> &placers)
    {
        placersCount = placers.size();
        placerCurrent = 0;

        Solution *best = NULL;
        bool stop = false;
        std::set<Placer*> workers;

        while (placers.size() || workers.size()) {
            while (placers.size() && workers.size() < nbThreads) {
                Placer *placer = placers.back();
                placers.pop_back();

                if (!stop && !cancel) {
                    workers.insert(placer);
                    placer->placeThreaded();
                } else {
                    delete placer;   // never started
                }
            }

            vector<Placer*> toDelete;
            for (auto placer : workers) {
                if (placer->solution != NULL) {
                    Solution *solutionTmp = placer->solution;

                    if (best == NULL || solutionTmp->score() < best->score()) {
                        best = solutionTmp;
                    } else {
                        delete solutionTmp;
                    }

                    if (best->countPlates() == 1) {
                        stop = true;
                    }

                    placerCurrent++;
                    toDelete.push_back(placer);
                }
            }

            for (auto placer : toDelete) {
                workers.erase(placer);
                delete placer;
            }

            ms_sleep(50);
        }

        return best;
    }

    void Request::consolidateSolution()
    {
        if (solution == NULL) {
            return;
        }

        // Try to repack every part into one fewer plate, then keep going. The
        // greedy already first-fits each part onto the earliest plate, so the
        // only way to drop a plate is to re-pack with rearrangement; we explore
        // a few orderings (the base run may have used just one) and accept a
        // result only when it strictly reduces the plate count.
        bool improved = true;
        while (improved && solution->countPlates() > 1) {
            improved = false;
            int target = solution->countPlates() - 1;

            std::vector<int> orderings;
            if (tallCenter) {
                // Keep the centre bias; vary rotation for a little diversity.
                orderings.push_back(PLACER_SORT_HEIGHT_DEC);
                orderings.push_back(PLACER_SORT_HEIGHT_DEC);
            } else {
                orderings.push_back(PLACER_SORT_SURFACE_DEC);
                orderings.push_back(PLACER_SORT_SURFACE_INC);
                for (int s=0; s<16; s++) {
                    orderings.push_back(PLACER_SORT_SHUFFLE);
                }
            }

            for (size_t oi=0; oi<orderings.size(); oi++) {
                Placer placer(this);
                placer.setMaxPlates(target);
                placer.sortParts(orderings[oi]);
                placer.setGravityMode(PLACER_GRAVITY_YX);
                placer.setRotateOffset(0);
                placer.setRotateDirection((int)oi & 1);
                if (tallCenter) {
                    placer.setScoreMode(PLACER_SCORE_CENTER);
                }

                Solution *candidate = placer.place();
                if (candidate != NULL && candidate->countPlates() <= target) {
                    _log("- Consolidated: %d -> %d plates\n",
                            target + 1, candidate->countPlates());
                    delete solution;
                    solution = candidate;
                    improved = true;
                    break;
                } else if (candidate != NULL) {
                    delete candidate;
                }
            }
        }

        if (!cancel && solution != NULL) {
            plates = solution->countPlates();
        }
    }

    void Request::process()
    {
        if (hasError) {
            cerr << "! Can't process: " << error << endl;
            return;
        }

        solve();

        if (consolidate) {
            consolidateSolution();
        }

        if (!cancel && !hasError && solution != NULL) {
            _log("* Solution\n");
            _log("- Plates: %d\n", solution->countPlates());
            _log("- Score: %g\n", solution->score());
            writeFiles(solution);
        }
    }

    void Request::processFit(double idealWmm, double idealHmm, double stepMm, int targetPlates)
    {
        if (cancel) {
            return;
        }
        if (targetPlates < 1) {
            targetPlates = 1;
        }

        // The configured plate size is the physical maximum to grow into.
        bool circle = (plateMode == PLATE_MODE_CIRCLE);
        double maxW = circle ? plateDiameter : plateWidth;
        double maxH = circle ? plateDiameter : plateHeight;
        double idealW = idealWmm * 1000.0;
        double idealH = idealHmm * 1000.0;
        if (circle) {
            // A circular bed has a single dimension; grow the diameter.
            idealH = idealW;
        }
        double step = stepMm * 1000.0;
        if (step <= 0) {
            step = 10000.0;
        }
        // Never start above (or grow beyond) the physical bed.
        if (idealW > maxW) idealW = maxW;
        if (idealH > maxH) idealH = maxH;

        // Each axis grows from its own ideal toward its own maximum by `step`,
        // both indexed by a single step counter so the search stays 1-D and the
        // plate area is monotonic in the counter (needed for the binary search).
        auto axisAt = [&](double ideal, double mx, int t) -> double {
            double v = ideal + t * step;
            return v < mx ? v : mx;
        };
        int steps = 0;
        while (axisAt(idealW, maxW, steps) < maxW || axisAt(idealH, maxH, steps) < maxH) {
            steps++;
        }
        // Valid step indices are 0..steps; index `steps` is the full bed size.

        // Apply a step: set the plate dimensions and cheaply re-filter every
        // already-loaded part. Returns false if a part no longer fits.
        auto applyT = [&](int t) -> bool {
            double w = axisAt(idealW, maxW, t);
            double h = axisAt(idealH, maxH, t);
            if (circle) {
                plateDiameter = w;
            } else {
                plateWidth = w;
                plateHeight = h;
            }
            bool feasible = true;
            for (auto &kv : parts) {
                if (kv.second->applyPlateSize(circle ? plateDiameter : plateWidth,
                                              circle ? plateDiameter : plateHeight) == 0) {
                    feasible = false;
                }
            }
            return feasible;
        };

        // Solve at a step; returns the plate count, or -1 if a part cannot fit.
        auto platesAt = [&](int t) -> int {
            if (!applyT(t)) {
                return -1;
            }
            _log("* Trying plate size %g x %g mm\n",
                    (circle ? plateDiameter : plateWidth) / 1000.0,
                    (circle ? plateDiameter : plateHeight) / 1000.0);
            solve();
            int n = (solution != NULL) ? solution->countPlates() : -1;
            if (n > 0) {
                _log("- Fits in %d plate(s)\n", n);
            }
            return n;
        };

        // Load the parts once, at the maximum (bed) size. Geometry is loaded
        // here; subsequent sizes only re-run the cheap fit filter.
        if (circle) {
            plateDiameter = maxW;
        } else {
            plateWidth = maxW;
            plateHeight = maxH;
        }
        hasError = false;
        error = "";
        readPartsFromString(partsText);
        if (hasError) {
            cerr << "! Can't process: " << error << endl;
            return;
        }

        // Fewest plates is achieved at the largest size. We accept the target
        // count, grown to whatever is actually reachable ("start at target,
        // add a plate only if forced").
        int nMax = platesAt(steps);
        if (nMax < 0) {
            hasError = true;
            error = "Parts don't fit even at the maximum plate size";
            cerr << "! " << error << endl;
            return;
        }
        int acceptable = targetPlates > nMax ? targetPlates : nMax;

        // Binary-search the smallest step whose plate count is within the
        // acceptable bound (plate count is monotonic non-increasing in size).
        int lo = 0, hi = steps, chosen = steps;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            int n = platesAt(mid);
            if (n > 0 && n <= acceptable) {
                chosen = mid;
                hi = mid - 1;
            } else {
                lo = mid + 1;
            }
        }

        // Ensure the final solution corresponds to the chosen step.
        platesAt(chosen);

        if (consolidate) {
            consolidateSolution();
        }

        if (!cancel && !hasError && solution != NULL) {
            _log("* Solution\n");
            _log("- Packed size: %g x %g mm\n",
                    (circle ? plateDiameter : plateWidth) / 1000.0,
                    (circle ? plateDiameter : plateHeight) / 1000.0);
            _log("- Plates: %d\n", solution->countPlates());
            _log("- Score: %g\n", solution->score());

            // Restore the physical bed so the 3MF plate grid and declared bed
            // match the real printer bed; the fit search only governs how
            // tightly the parts pack within each plate.
            if (circle) {
                plateDiameter = maxW;
            } else {
                plateWidth = maxW;
                plateHeight = maxH;
            }
            writeFiles(solution);
        }
    }
}
