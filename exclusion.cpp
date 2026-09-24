
#include "core/buddha.h"
#include "core/settings_parser.h"
#include <thread>
using namespace std;


typedef complex<double> complex_type;
std::default_random_engine generator;
std::uniform_real_distribution<double> uniform(0,1);

volatile int finish = 0;


unsigned int loop ( settings& s, mandelbrot<complex_type>& core, vector<unsigned int>& border, unsigned int samples ) {
	vector<complex_type> seq;
	seq.resize(s.high + 1);
	unsigned int errors = 0;
	unsigned int calculated;

	for ( unsigned int i = 0; i < samples && !finish; ++i ) {
        complex_type z ( uniform(generator) * core.radius(), 
                         uniform(generator) * core.radius() );

        unsigned int j = uniform(generator) * border.size();
        seq[0] = core.point(border[j]) + z;

        if (core.evaluate(seq, calculated ) == -1 && core.evaluate(seq) != -1) {
            errors++;
            core.data[core.index(seq[0])] = false;
            border[j] = border.back();
            border.pop_back();
        }
    }

    return errors;
}

void refine ( mandelbrot<complex_type>& core, settings& s ) {
	unsigned int total = 0;
	unsigned int errors = 0;
    unsigned int samples = 100000;


    while ( !finish ) {
    	vector<unsigned int> border = core.border();
    	unsigned int bordersize = border.size();
    	errors += loop( s, core, border, samples );
    	total += samples;
    	BOOST_LOG_TRIVIAL(info) << errors << " errors on " << total << " samples (" 
    							<< double(errors) / total << "%, border size " << bordersize <<")";
    }
}



int main ( int argc, char** argv ) {
    settings_parser parser( argc, argv );
    settings s = parser();
    mandelbrot<complex_type> core( s );

    if ( !core.load()) 
   		core.exclusion();


    std::thread t(refine, std::ref(core), std::ref(s));



    // Wait for signal indicating time to shut down.
    sigset_t wait_mask;
    sigemptyset(&wait_mask);
    sigaddset(&wait_mask, SIGINT);
    sigaddset(&wait_mask, SIGQUIT);
    sigaddset(&wait_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
    int sig = 0;
    sigwait(&wait_mask, &sig);

    BOOST_LOG_TRIVIAL(debug) << "interrupt signal (" << sig << ") received";
    finish = 1;
    t.join();

    core.save();


    return 0;
}
