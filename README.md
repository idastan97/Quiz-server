The scecification of the task: spec.docx

compiling server:
	gcc connectsock.c passivesock.c quizserver.c -o server.out -pthread

running server:
	./server.out [port](optional)

running client:
	java -jar QuizClient-6.jar localhost [portnumber]

