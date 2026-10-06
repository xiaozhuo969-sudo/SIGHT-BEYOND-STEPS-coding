"""Identify the five wearable LEDs over USB; never sends ARM or TAKEOFF."""
import argparse, threading, time
import serial

parser=argparse.ArgumentParser(description='逐灯识别 GPIO12/13/14/21/47；输入 -1 恢复业务指示，q 退出。')
parser.add_argument('--port',required=True,help='手控制器 USB 串口，例如 COM7；先关闭占用此串口的上位机。')
args=parser.parse_args()
with serial.Serial(args.port,115200,timeout=.2) as port:
    stopped=threading.Event()
    def monitor():
        while not stopped.is_set():
            try: line=port.readline().decode('utf-8',errors='replace').strip()
            except serial.SerialException: return
            if 'LED identify' in line: print('\n'+line,flush=True)
    reader=threading.Thread(target=monitor,daemon=True);reader.start()
    print('需使用已编译的新手控固件，飞机保持锁定。每路点亮 10 秒，期间不能起飞。')
    print('已定义 GPIO12=飞控通信、13=相机通信、14=左倾、21=右倾、47=下降；可核对实物位置／大小。')
    try:
        while True:
            answer=input('点亮 GPIO（12/13/14/21/47），-1 恢复业务指示，q 退出：').strip()
            if answer.lower()=='q':break
            if answer not in ('12','13','14','21','47','-1'):
                print('请输入列表中的编号。');continue
            port.write(f'@L,TEST,{answer}\n'.encode('ascii'));port.flush()
    finally:
        port.write(b'@L,TEST,-1\n');port.flush();time.sleep(.3)
        stopped.set();reader.join(timeout=1)
