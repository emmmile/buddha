#include <settings.h>

void settings::indirect_settings ( ) {
    rangere = w / scale;
    rangeim = h / scale;
    minre = cre - rangere * 0.5;
    maxre = cre + rangere * 0.5;
    minim = cim - rangeim * 0.5;
    maxim = cim + rangeim * 0.5;
    high = max( max( highr, highg ), highb );
    low = min( min(lowr, lowg), lowb);
    size = w * h / 2;

    realLightness = (float) lightness / ( maxLightness - lightness + 1 ) * 0.5;
    realContrast = (float) contrast / (maxContrast) * 0.7;

    //compile_formula();
}



void settings::compile_formula ( ) {
    ofstream source("/tmp/code.cpp");

    source << "#include <complex.h>\nusing namespace std;\n"
           << "extern \"C\" void next_point ( complex<double>& z, complex<double>& c ) {\n"
           << "  " << formula << ";\n"
           << "}" << endl;
    source.close();

    system("rm -f /tmp/code.so");
    system("c++ -O3 -mtune=native -ffast-math -funroll-loops -Wall /tmp/code.cpp -o /tmp/code.so -shared -fPIC -std=c++11");

    char *error;
    void* handle = dlopen("/tmp/code.so", RTLD_NOW);
    if (!handle) {
        BOOST_LOG_TRIVIAL(fatal) << dlerror();
        exit(EXIT_FAILURE);
    }

    dlerror();    // Clear any existing error
    next_point = (void (*)(complex<double>&, complex<double>&)) 
                 dlsym(handle, "next_point");

    if ((error = dlerror()) != NULL) {
        BOOST_LOG_TRIVIAL(fatal) << error;
        exit(EXIT_FAILURE);
    }

    BOOST_LOG_TRIVIAL(debug) << "successfully loaded formula: `" << formula << "'";
}


void settings::dump ( ) const {
    BOOST_LOG_TRIVIAL(debug) << "low: " << low << ", high " << high;
    BOOST_LOG_TRIVIAL(debug) << "cre: " << cre << ", cim " << cim;
    BOOST_LOG_TRIVIAL(debug) << "maxre: " << maxre << ", maxim " << maxim;
    BOOST_LOG_TRIVIAL(debug) << "minre: " << minre << ", minim " << minim;
    BOOST_LOG_TRIVIAL(debug) << "width: " << w << ", height: " << h;
    BOOST_LOG_TRIVIAL(debug) << "scale: " << scale << ", threads: " << threads;
    BOOST_LOG_TRIVIAL(debug) << "lightness: " << lightness << ", contrast: " << contrast;
    BOOST_LOG_TRIVIAL(debug) << "formula: `" << formula << "'";
}