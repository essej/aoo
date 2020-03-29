# make pdf from source for commitment
#
# to be replaced by sphinx tools in future
DOCS=aoo.pdf

all: $(DOCS)

distclean: clean
	rm $(DOCS)

clean:
	-rm -v *~
	-rm -v *.backup

%.pdf: %.rst # %.bib
	rst2pdf "$<"

.PHONY:all clean 
