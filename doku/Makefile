# Makefile for doku files and 

%.tex : %.rst
	rst2latex $< > $@

%.pdf: %.tex
	pdflatex $<
	pdflatex $<
