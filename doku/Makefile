# Makefile for doku files and 

DOCS=aoo_protocol.pdf aoo_usecases.pdf

all: ${DOCS}

%.tex : %.rst
	rst2latex $< > $@

%.pdf: %.tex
	pdflatex $<
	pdflatex $<

clean:
	rm -f ${DOCS} *.out *aux *bbl *blg *log *toc *.ptb *.tod *.fls *.fdb_latexmk *.lof
