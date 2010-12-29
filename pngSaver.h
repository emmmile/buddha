
#ifndef PNGSAVER_H
#define PNGSAVER_H

#include <QThread>
#include "buddha.h"

// simply saves a PNG using a new thread


class PNGSaver : public QThread {
	Buddha* b;
	QString fileName;
public:
	void set ( Buddha* b, QString fileName );
	
	void run ( );
};

#endif
