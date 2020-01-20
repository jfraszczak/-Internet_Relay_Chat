import sys
import time
import threading

from PyQt5 import QtGui
from PyQt5.QtGui import *
from PyQt5.QtWidgets import *
from PyQt5 import QtCore
from PyQt5.QtCore import QSize

import socket

#KLIENT

#utworzenie gniazda i połączenie się z serwerem
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('192.168.47.119', 1234))

#zmienne globalne
user = None
room = None
ex = None

inRoom = False  #infromuje czy użytkownik już wszedł do pokoju

rooms = []
users = []
messages = []

#implementacja poprawnego odczytywania wiadomości (nieprzypadkowego)
def receive():
    fullMsg = ''
    msg = 'X'
    while msg[-1] != '$':
        msg = sock.recv(128)
        msg = msg.decode("utf-8")
        fullMsg += msg

    return fullMsg

#odczytuje obecny stan serwera zwracając tablicę z istniejącymi pokojami, użytkownikami oraz wiadomościami
def read_server_response(response):
    if response[0] == '#':
        rooms = []
        passwords = []
        users = []
        messages = []

        i = 1
        while response[i] != '$':
            i += 1
            while response[i] != '@' and response[i] != '$':
                users_pom = []
                messages_pom = []
                room = ''
                while response[i] != '^':
                    room += response[i]
                    i += 1
                rooms.append(room)
                i += 1

                password = ''
                while response[i] != '%' and response[i] != '@' and response[i] != '$':
                    password += response[i]
                    i += 1
                passwords.append(password)
                i += 1

                if response[i - 1] == '%':
                    while response[i] != '%' and response[i] != '@' and response[i] != '$':
                        user = ''
                        while response[i] != ';' and response[i] != '%' and response[i] != '$' and response[i] != '@':
                            user += response[i]
                            i += 1
                        users_pom.append(user)
                        if response[i] == ';': i += 1
                    users.append(users_pom)
                    i += 1
                    
                    if response[i - 1] == '%':
                        while response[i] != '@' and response[i] != '$':
                            message = ''
                            while response[i] != ';' and response[i] != '@' and response[i] != '$':
                                message += response[i]
                                i += 1
                            messages_pom.append(message)
                            if response[i] == ';' : i += 1
                        messages.append(messages_pom)
                    else:
                        i -= 1
                        messages.append([])
                        
        return (rooms, passwords, users, messages)
    else: return -1

#odczytuje wysłaną wiadomość w formacie użytkownik:wiadomość i zwraca je osobno
def readMessage(message):
    user = ''
    msg = ''
    i = 0
    n = len(message)
    while message[i] != ':':
        user += message[i]
        i += 1
    for j in range(i + 1, n):
        msg += message[j]

    if msg[0] == 'X':
        user = ''

    return user, msg

#odczytuje wszystkie wiadomości z formatu użytkownik:wiadomość i zapisuje w tablicy obiektów Message
def formatMessages(messages_raw):
    global messages
    messages = []
    for message in messages_raw:
        u, m = readMessage(message)
        msg = Message(u, m)
        messages.append(msg)

#sprawdza czy pokój o zadanej nazwie istnieje
def roomExists(name, rooms):
    for room in rooms:
        if name == room:
            return True
    return False

def countUsers():
    global users
    return len(users)

#zapisuje w globalnych tablicach informacje o wszystkichh pokojach oraz użytkownikach i wiadomościach w obecnym pokoju
def update_server_state(response, room):
    global users
    global rooms
    state = read_server_response(response)
    if state != -1:
        rooms = state[0][:]
        for i in range(len(state[0])):
            if state[0][i] == room.name:
                users = state[2][i][:]
                messages_raw = state[3][i][:]
                formatMessages(messages_raw)
        return 0
    return -1

#prosi o stan serwera
def askForServerState(room):
    sock.settimeout(0.2)
    try:
        sock.recv(16384)
    except socket.timeout as e:
        msg = ''
    sock.settimeout(None)
    msg = '#5 ' + room + '$'
    sock.send(bytes(msg, "utf-8"))
    msg = receive()
    response = read_server_response(msg)

    return response

#sprawdza czy podane hasło jest poprawne
def checkPassword(name, password):
    response = askForServerState(name)
    for i in range(len(response[0])):
        if name == response[0][i]:
            if password != response[1][i]:
                return False
            else:
                break
    return True

#sprawdza czy w pokoju nie ma już użytkownika o takim samym nicku
def checkUsers(nick):
    global room
    response = askForServerState(room.name)
    for i in range(len(response[0])):
        if response[0][i] == room.name:
            for user in response[2][i]:
                if user == nick:
                    return False
            break
    return True

#zeruje wszystkie dane o pokoju, używana podczas wychodzenia z pokoju
def resetRoom():
    global inRoom
    global rooms
    global users
    global messages
    global ex

    inRoom = False  # infromuje czy użytkownik już wszedł do pokoju

    rooms = []
    users = []
    messages = []

    ex.chat.previousLength = 0

class User:
    #wysyła do serwera wiadomość o połączeniu się nowego użytkownika
    def login(self, nick):
        if len(nick) < 5:
            flag = False
            communication = 'Chosen nickname is too short.\nNickname must consist of at least 5 characters.'
        else:
            if checkUsers(nick):
                flag = True
                communication = ''
                self.nick = nick
                msg = '#0 ' + nick + '$'
                sock.send(bytes(msg,"utf-8"))
                receive()
            else:
                flag = False
                communication = 12 * ' ' + 'Chosen nickname is already taken'

        return flag, communication

    def exitRoom(self):
        global room
        global user
        count = countUsers()
        time.sleep(0.1)
        msg = '#3 ' + room.name + '$'
        if count > 1:
            msgInfo = '#4 ' + room.name + '%' + 'X' + 'User ' + user.nick + ' left the room' + '$'
            sock.send(bytes(msgInfo, "utf-8"))
        time.sleep(0.1)
        sock.send(bytes(msg, "utf-8"))
        time.sleep(0.3)
        resetRoom()

class Room:
    #zapisuje dane obiektu pokój
    def tryJoin(self, name, password):
        if len(name) < 5:
            flag = False
            communication = 'Chosen room name is too short.\nRoom name must consist of at least 5 characters.'
        elif len(password) < 5:
            flag = False
            communication = 'Password is too short.\nPassword must consist of at least 5 characters.'
        else:
            if checkPassword(name, password):
                flag = True
                communication = ''
                self.name = name
                self.password = password
            else:
                flag = False
                communication = 21 * ' ' + 'Given password is wrong'

        return flag, communication

    #dołączenie do pokoju
    def join(self):
        # wysyła do serwera wiadomość o dołączeniu do pokoju
        msg = '#2 ' + self.name + '$'
        sock.send(bytes(msg, "utf-8"))
        msg = receive()
        response = read_server_response(msg)
        #jeśli taki pokój nie istnieje to tworzy nowy
        if not roomExists(self.name, response[0]):
            msg = '#1 ' + self.name + '^' + self.password + '$'
            sock.send(bytes(msg, "utf-8"))         #wysyła do serwera wiadomość o utworzeniu nowego pokoju
            msg = receive()

        global user
        msgInfo = '#4 ' + self.name + '%' + 'X' + 'User ' + user.nick + ' joined the room' + '$'
        sock.send(bytes(msgInfo, "utf-8"))
        time.sleep(0.3)

        global inRoom
        update_server_state(msg, self)      #pobranie wszystkich danych o serwerze
        inRoom = True                       #zapisanie w globalnej zmiennej informacji o dołączeniu do pokoju
        time.sleep(0.3)


class Message:
    def __init__(self, nick, message):
        self.nick = nick
        self.message = message

    #wysyła do serwera wiadomość
    def send(self, room):
        msg = '#4 ' + room.name + '%' + self.message + '$'
        sock.send(bytes(msg,"utf-8"))


#GUI

#ekran logowania do pokoju
class Interface(QWidget):
    def __init__(self):
        super().__init__()
        self.title = 'IRC'
        self.left = 200
        self.top = 80
        self.width = 1500
        self.height = 900

        self.inRoom = False
        self.nickSet = False

        self.RoomUI()

    def closeEvent(self, event):
        global user
        #user.exitRoom()
        sys.exit()

    def buttonPressed(self):
        global room
        room = Room()
        self.inRoom, communication = room.tryJoin(self.textbox1.text(), self.textbox2.text())
        if self.inRoom:
            self.window = LoginWindow()
        else:
            self.window = Error(communication)

    def RoomUI(self):
        self.setWindowTitle(self.title)
        self.setGeometry(self.left, self.top, self.width, self.height)

        self.label = QLabel(self)
        pixmap = QPixmap('image.jpg')
        pixmap = pixmap.scaled(1500, 937)
        self.label.setPixmap(pixmap)

        font = QtGui.QFont("Calibri", 11)

        self.textbox1 = QLineEdit(self)
        self.textbox1.move(100, 200)
        self.textbox1.resize(300, 40)
        self.textbox1.setFont(font)

        self.textbox2 = QLineEdit(self)
        self.textbox2.move(100, 290)
        self.textbox2.resize(300, 40)
        self.textbox2.setFont(font)

        self.label1 = QLabel(self)
        self.label1.setText('Room')
        self.label1.move(100, 170)
        self.label1.setFont(font)

        self.label2 = QLabel(self)
        self.label2.setText('Password')
        self.label2.move(100, 260)
        self.label2.setFont(font)

        self.label3 = QLabel(self)
        self.label3.setText('JOIN THE ROOM')
        self.label3.move(100, 50)
        self.label3.setFont(font)

        self.button = QPushButton('Join', self)
        self.button.setGeometry(100, 400, 120, 60)
        self.button.clicked.connect(self.buttonPressed)

        font = QtGui.QFont("Calibri", 15)
        self.button.setFont(font)

        font = QtGui.QFont("Calibri", 20)
        font.setBold(True)
        self.label3.setFont(font)

        self.label1.setStyleSheet("color: white")
        self.label2.setStyleSheet("color: white")
        self.label3.setStyleSheet("color: white")
        self.button.setStyleSheet("background-color: white");

        self.show()

#ekran informujący o błędach
class Error(QWidget):
    def __init__(self, communication):
        super().__init__()
        self.title = 'Error'
        self.communication = communication
        self.left = 750
        self.top = 250
        self.width = 400
        self.height = 200

        self.initUI()

    def initUI(self):
        self.setWindowTitle(self.title)
        self.setGeometry(self.left, self.top, self.width, self.height)

        self.label = QLabel(self)
        pixmap = QPixmap('image.jpg')
        self.label.setPixmap(pixmap)
        self.label.move(-300, -200)

        font = QtGui.QFont("Calibri", 11)
        self.label1 = QLabel(self)
        self.label1.setText(self.communication)
        self.label1.move(30, 70)
        self.label1.setFont(font)
        self.label1.setStyleSheet("color: white");

        self.show()

#ekran ustawiania nazwy użytkownika
class LoginWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.title = 'Login'
        self.left = 650
        self.top = 200
        self.width = 600
        self.height = 300

        self.nickSet = False

        self.initUI()

    def buttonPressed(self):
        global user
        global room
        global ex
        user = User()
        self.nickSet, communication = user.login(self.textbox1.text())
        if self.nickSet:
            ex.chat = ChatWindow()
            ex.nickSet = True
            time.sleep(0.1)
            room.join()
            self.hide()
        else:
            self.window = Error(communication)


    def initUI(self):
        self.setWindowTitle(self.title)
        self.setGeometry(self.left, self.top, self.width, self.height)

        self.label = QLabel(self)
        pixmap = QPixmap('image.jpg')
        #pixmap = pixmap.scaled(1500, 937)
        self.label.setPixmap(pixmap)
        self.label.move(-300, -200)

        font = QtGui.QFont("Calibri", 11)

        self.textbox1 = QLineEdit(self)
        self.textbox1.move(150, 80)
        self.textbox1.resize(300, 40)
        self.textbox1.setFont(font)

        self.label1 = QLabel(self)
        self.label1.setText('Choose your nickname')
        self.label1.move(150, 40)
        self.label1.setFont(font)
        self.label1.setStyleSheet("color: white")

        self.button = QPushButton('Start chatting', self)
        self.button.setGeometry(200, 170, 200, 70)
        self.button.clicked.connect(self.buttonPressed)
        self.button.setStyleSheet("background-color: white");

        font = QtGui.QFont("Calibri", 15)
        self.button.setFont(font)

        self.show()

#ekran chatu
class ChatWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.title = 'IRC'
        self.left = 200
        self.top = 80
        self.width = 1500
        self.height = 900

        self.previousLength = 0

        self.ChatUI()

    def closeEvent(self, event):
        global user
        user.exitRoom()
        event.accept() # let the window close

    def update(self):
        global messages
        global user
        for i in range(self.previousLength, len(messages)):
            font = QtGui.QFont("Calibri", 11)
            label1 = QLabel(self)
            if user.nick == messages[i].nick:
                label1.setText('<font size=3><b>' + 'You' + ': </b></font>' + messages[i].message)
            else:
                label1.setText('<font size=3><b>' + messages[i].nick + ': </b></font>' + messages[i].message)
            if messages[i].message[0] == 'X':
                messages[i].message = messages[i].message[1:]
                label1.setText(messages[i].message)
                label1.setStyleSheet("color: gray");
                font.setBold(True)
            label1.setFont(font)
            self.formLayout.addRow(label1)
            self.textbox1.setText('')
        self.previousLength = len(messages)

    def buttonPressed(self):
        text = self.textbox1.text()
        if len(text) > 0:
            msg = Message(user.nick, self.textbox1.text())
            msg.send(room)

    def ChatUI(self):
        self.setWindowTitle(self.title)
        self.setGeometry(self.left, self.top, self.width, self.height)

        oImage = QImage("image.jpg")
        sImage = oImage.scaled(QSize(1500, 937))
        palette = QPalette()
        palette.setBrush(10, QBrush(sImage))
        self.setPalette(palette)

        self.formLayout = QFormLayout()
        self.groupBox = QGroupBox()
        self.scroll = QScrollArea()
        self.groupBox.setLayout(self.formLayout)
        self.scroll = QScrollArea()
        self.scroll.setWidget(self.groupBox)
        self.scroll.setWidgetResizable(True)
        self.scroll.setFixedHeight(600)
        self.scroll.setFixedWidth(1500)
        self.layout = QVBoxLayout(self)
        self.layout.addWidget(self.scroll)
        self.scroll.setStyleSheet("background-color: white");

        font = QtGui.QFont("Calibri", 11)
        self.textbox1 = QLineEdit(self)
        self.textbox1.move(12, 800)
        self.textbox1.resize(1300, 40)
        self.textbox1.setFont(font)

        self.button = QPushButton('Send', self)
        self.button.setGeometry(1320, 799, 195, 42)
        self.button.clicked.connect(self.buttonPressed)
        self.button.setStyleSheet("background-color: white");

        self.label = QLabel(self)
        self.label.setText('#' + room.name)
        self.label.setGeometry(20, 50, 1600, 50)
        self.label.setStyleSheet("color: white");

        font = QtGui.QFont("Calibri", 22)
        font.setBold(True)
        self.label.setFont(font)
        font = QtGui.QFont("Calibri", 11)
        font.setBold(True)
        self.button.setFont(font)


        self.show()

#co jakis czas aktualizuje zawartość chatroomu
def update():
    if ex.nickSet:
        ex.chat.update()

#wątek Gui
def runGui():
    app = QApplication(sys.argv)
    global ex
    ex = Interface()

    timer = QtCore.QTimer()
    timer.timeout.connect(update)
    timer.start(100)

    sys.exit(app.exec_())

#wątek nasłuchującego klienta (zaczyna nasłuchiwać dopiero jak wejdzie od pokoju)
def runClient():
    while True:
        global inRoom
        if inRoom:
            sock.settimeout(0.5)
            try:
                msg = receive()
            except socket.timeout as e:
                msg = ''
            sock.settimeout(None)

            if len(msg) > 0:
                print('Otrzymano wiadomosc: ', msg)
                update_server_state(msg, room)      #jeśli otrzymano wiadomość to jest aktualizowany stan serwera i zawartość pokoju

thread1 = None
thread2 = None

#odpalenie obu wątków
if __name__ == '__main__':
    thread1 = threading.Thread(target = runGui)
    thread2 = threading.Thread(target = runClient)
    thread1.start()
    thread2.start()
