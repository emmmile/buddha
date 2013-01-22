# Buddha++
## A Qt-based, multi-threaded BuddhaBrot navigator

The program as a number of features:
* It's possible to set various parameters to change the characteristics of the rendered image, 
like the iteration depth on the 3 color channels, brightness and contrast, [preview](emmmile/buddha/master/resources/gui.png).
* The program deal particularly well with high iteration depths, thanks to a big optimization in the points calculation.
I later found that this is a variation of the [Brent's method](http://en.wikipedia.org/wiki/Cycle_detection#Brent.27s_algorithm)
* Saving the image and (work in progress) save and reload all the calculation parameters.
* Dinamically change the number of threads calculating the fractal and adjust the number of frames drawn every second.


## TODO list
* Add an option for executing without interface (or make it more modular in such a way I can build two separate executables with shared code)
* Add the possibility to run it distributed (via command line, maybe with a gui server)
