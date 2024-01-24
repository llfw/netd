.SUFFIXES: .c .cc .ccm .o .pcm

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c $< -MMD -MF ${.OBJDIR}/$@.d -o ${.OBJDIR}/$@

.cc.o:
	${CXX} ${CPPFLAGS} ${CXXFLAGS} -c $< -MMD -MF ${.OBJDIR}/$@.d -o ${.OBJDIR}/$@

.ccm.o:
	${CXX} ${CPPFLAGS} ${CXXFLAGS} -fmodule-output -c $< -o ${.OBJDIR}/$@
