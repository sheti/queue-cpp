﻿queue
=====

Менеджер очереди на C++. Позволяет выполнять задания запуская скрипты. 
Задания получает из unix socket. Сразу же создается дочерний процесс и скрипт продолжает принимать задания.

Чтобы запустить задание в ожидающий unix socket посылается строка параметров:

    Exec: task_name\n
    Param1: Value1\n
    Param2: Value2\n
    \n

список параметров заканчивается пустой строкой с переводом строки. Опязательно должен присутствовать один 
параметр **Exec** с названием задания. Список заданий задаётся в конфигурационном файле.
