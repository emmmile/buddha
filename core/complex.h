/*
 * Copyright (c) 2010, Emilio Del Tessandoro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY EMILIO DEL TESSANDORO o ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL EMILIO DEL TESSANDORO BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef COMPLEX_H
#define COMPLEX_H


/// semplified structure to store complex numbers and generate random complex numbers
template<class F>
class simple_complex {
private:
    F re;
    F im;

public:
    typedef F value_type;

    simple_complex( value_type re = 0.0, value_type im = 0.0 ) {
        this->re = re;
        this->im = im;
    }

    inline simple_complex& operator = ( const simple_complex& z ) {
        this->re = z.re;
        this->im = z.im;
        return *this;
    }

    inline value_type real ( ) const {
        return re;
    }

    inline value_type imag ( ) const {
        return im;
    }

    friend const simple_complex operator - (const simple_complex& z, const simple_complex& t ) {
        return simple_complex( z.re - t.re, z.im - t.im );
    }

    friend const simple_complex operator + (const simple_complex& z, const simple_complex& t ) {
        return simple_complex( z.re + t.re, z.im + t.im );
    }

    friend value_type norm ( const simple_complex& c ) {
        return c.real() * c.real() + c.imag() * c.imag();
    }

    friend simple_complex conj ( const simple_complex& c ) {
        return simple_complex( c.real(), -c.imag() );
    }
};


#endif
