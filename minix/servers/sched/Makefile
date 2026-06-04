.include <bsd.own.mk>

# Makefile for Scheduler (SCHED)
PROG=	sched
SRCS=	main.c schedule.c utility.c

DPADD+=	${LIBSYS}
LDADD+=	-lsys

CPPFLAGS.schedule.c+=	-I${NETBSDSRCDIR}/minix

.include <minix.service.mk>
