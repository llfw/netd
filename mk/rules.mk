.if !target(__rules.mk__)
__rules.mk__:

.SUFFIXES: .c .cc .ccm .o .pcm .cc.o .pcm.o .module.o

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c $< -MMD -MF ${.OBJDIR}/$@.d -o ${.OBJDIR}/$@

.cc.o:
	${CXX} ${CPPFLAGS} ${CXXFLAGS} -c $< -MMD -MF ${.OBJDIR}/$@.d -o ${.OBJDIR}/$@

.ccm.pcm:
	${CXX} --precompile ${CPPFLAGS} ${CXXFLAGS} -fmodule-output -c $< -o ${.OBJDIR}/$@

.ccm.module.o:
	${CXX} ${CPPFLAGS} ${CXXFLAGS} -fmodule-output -c $< -o ${.OBJDIR}/$@

.endif
