#include <iostream>
#include <fcntl.h>
#include <cstring>
#include <vector>
#if defined(_WIN32) || defined(_WIN64)
#include "wingetopt.h"
#else
#include <getopt.h>
#endif
#define _USE_MATH_DEFINES
#include <math.h>
#include "stl/StlFactory.h"
#include "Request.h"
#include "util.h"
#include "log.h"

using namespace std;
using namespace Plater;

// Reorder argv so option arguments precede operands (the conf file), letting
// options appear in any position. BSD/macOS getopt otherwise stops parsing at
// the first non-option, silently ignoring options placed after the file.
static void permuteArgs(int argc, char *argv[], const char *optstring)
{
    std::vector<char*> opts, operands;
    for (int i = 1; i < argc; i++) {
        char *tok = argv[i];
        if (tok[0] == '-' && tok[1] != '\0') {
            opts.push_back(tok);
            // If this option takes a separate argument, pull in the next token.
            for (int j = 1; tok[j]; j++) {
                const char *p = strchr(optstring, tok[j]);
                bool takesArg = (p != NULL && *(p + 1) == ':');
                if (takesArg) {
                    if (tok[j + 1] == '\0' && i + 1 < argc) {
                        opts.push_back(argv[++i]);   // arg is the next token
                    }
                    break;                            // else arg is attached
                }
            }
        } else {
            operands.push_back(tok);                  // conf file or "-"
        }
    }
    int k = 1;
    for (char *o : opts)     argv[k++] = o;
    for (char *o : operands) argv[k++] = o;
}

void help()
{
    cerr << "Plater v1.0 (https://github.com/RobotsWar/Plater)" << endl;
    cerr << "Usage: plater [options] plater.conf" << endl;
    cerr << "(Use - to read from stdin)" << endl;
    cerr << endl;
    cerr << "-h: Display this help" << endl;
    cerr << "-v: Verbose mode" << endl;
    cerr << "-b size: bed plate size in mm (topview, 2D); default 150. A single value" << endl;
    cerr << "         is square; use AxB (e.g. 300x200) for a rectangular bed" << endl;
    cerr << "-D diameter: Set the plate diameter, in mm. If set, this will put the plate in circular mode" << endl;
    cerr << "-j precision: Sets the precision (in mm, default: 0.5)" << endl;
    cerr << "-s spacing: Change the spacing between parts (in mm, default: 1.5)" << endl;
    cerr << "-d delta: Sets the interval of place grid (in mm, default: 1.5)" << endl;
    cerr << "-r rotation: Sets the interval of rotation (in °, default: 90)" << endl;
    cerr << "-S: Trying multiple sort possibilities" << endl;
    cerr << "-R random: Sets the number of random (shuffled parts) iterations (only with -S)" << endl;
    cerr << "-o pattern: output file pattern (default: plate_%03d)" << endl;
    cerr << "-p: will output ppm of the plates" << endl;
    cerr << "-m: output a single 3MF file containing all the plates" << endl;
    cerr << "-O name: name of the 3MF output file (default: plate.3mf); .3mf added if missing" << endl;
    cerr << "-A algorithm: placement algorithm (default: brute). One of:" << endl;
    cerr << "     brute    - original full brute force (hole-aware)" << endl;
    cerr << "     pruned   - hole-aware pruned brute force (same packing as brute, faster)" << endl;
    cerr << "     skyline  - skyline bottom-left drop (fastest; no hole filling; rectangular plates)" << endl;
    cerr << "     contact  - skyline with max-contact scoring (denser; no hole filling)" << endl;
    cerr << "-C: consolidation pass - try to empty the sparsest plate into the others' gaps" << endl;
    cerr << "-T: place taller parts toward the centre of the plate (helps print reliability)" << endl;
    cerr << "Fit search (grow the plate from an ideal size up to the bed size -b):" << endl;
    cerr << "  -i ideal: ideal (smallest) plate size in mm; enables the fit search." << endl;
    cerr << "            A single value is square; use WxH (e.g. 250x180) to grow each axis" << endl;
    cerr << "            from its own ideal toward -b independently" << endl;
    cerr << "  -g step: growth step in mm while searching (default: 10)" << endl;
    cerr << "  -N plates: number of plates to target first, grows if needed (default: 1)" << endl;
    cerr << "-t threads: sets the number of threads (default 1)" << endl;
    cerr << "-c: enables the output of plates.csv containing plates infos" << endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    int index;
    Request request;

    bool fitMode = false;
    double fitIdealW = 0;
    double fitIdealH = 0;
    double fitStep = 10;
    int fitTarget = 1;

    const char *optstring = "hvs:d:r:pmA:CTO:j:d:o:b:R:D:t:Sci:g:N:";
    permuteArgs(argc, argv, optstring);

    while ((index = getopt(argc, argv, optstring)) != -1) {
        switch (index) {
            case 'h':
                help();
                break;
            case 'v':
                increaseVerboseLevel();
                break;
            case 's':
                request.spacing = atof(optarg)*1000;
                break;
            case 'd':
                request.delta = atof(optarg)*1000;
                break;
            case 'r':
                request.deltaR = DEG2RAD(atof(optarg));
                break;
            case 'p':
                request.mode = REQUEST_PPM;
                break;
            case 'm':
                request.mode = REQUEST_3MF;
                break;
            case 'A': {
                string algo = string(optarg);
                if (algo == "brute") {
                    // defaults: no flags
                } else if (algo == "pruned") {
                    request.prunedBrute = true;
                } else if (algo == "skyline") {
                    request.skyline = true;
                } else if (algo == "contact") {
                    request.skyline = true;
                    request.contact = true;
                } else {
                    cerr << "Unknown algorithm '" << algo
                         << "' (expected: brute, pruned, skyline, contact)" << endl;
                    help();
                }
                break;
            }
            case 'C':
                request.consolidate = true;
                break;
            case 'T':
                request.tallCenter = true;
                break;
            case 'j':
                request.precision = atof(optarg)*1000;
                break;
            case 'o':
                request.pattern = string(optarg);
                break;
            case 'O':
                request.outputFile = string(optarg);
                break;
            case 'b': {
                // A single value is a square bed; "AxB" (or "A,B") sets a
                // rectangular bed width x height.
                string v = string(optarg);
                size_t sep = v.find_first_of("xX,");
                if (sep != string::npos) {
                    request.plateWidth = atof(v.substr(0, sep).c_str())*1000;
                    request.plateHeight = atof(v.substr(sep + 1).c_str())*1000;
                } else {
                    request.plateWidth = request.plateHeight = atof(v.c_str())*1000;
                }
                break;
            }
            case 'S':
                request.sortMode = REQUEST_MULTIPLE_SORTS;
                break;
            case 'R':
                request.randomIterations = atoi(optarg);
                break;
            case 'D':
                request.plateMode = PLATE_MODE_CIRCLE;
                request.plateDiameter = atof(optarg)*1000;
                break;
            case 't':
                request.nbThreads = atoi(optarg);
                break;
            case 'c':
                request.platesInfo = true;
                break;
            case 'i': {
                // A single value is a square ideal; "WxH" (or "W,H") gives a
                // per-axis ideal that grows toward -W/-H independently.
                string v = string(optarg);
                size_t sep = v.find_first_of("xX,");
                if (sep != string::npos) {
                    fitIdealW = atof(v.substr(0, sep).c_str());
                    fitIdealH = atof(v.substr(sep + 1).c_str());
                } else {
                    fitIdealW = fitIdealH = atof(v.c_str());
                }
                fitMode = true;
                break;
            }
            case 'g':
                fitStep = atof(optarg);
                break;
            case 'N':
                fitTarget = atoi(optarg);
                break;
        }
    }

    if (optind != argc) {
        string filename = string(argv[optind]);
        if (filename == "-") {
            request.readFromStdin();
        } else {
            request.readFromFile(filename);
        }
    } else {
        help();
    }

    if (fitMode) {
        request.processFit(fitIdealW, fitIdealH, fitStep, fitTarget);
    } else {
        request.process();
    }

    return EXIT_SUCCESS;
}
