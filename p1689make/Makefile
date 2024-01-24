LIBS	= -lprivateucl

p1689make: p1689make.o
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o ${.OBJDIR}/$@ $> ${LIBS}

clean:
	rm -f ${.OBJDIR}/p1689make

.include "../mk/vars.mk"
