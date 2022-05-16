import requests
import sys
import time

from fake_useragent import UserAgent

def DownloadFile(url, filepath):
    with open(filepath, 'wb') as f:
        ua = UserAgent()
        headers = {'User-Agent': ua.chrome}
        print('Waiting for response...')
        response = requests.get(url, headers=headers, stream=True)
        total = response.headers.get('content-length')
        print('Downloading...')
        if total is None:
            f.write(response.content)
        else:
            downloaded = 0
            total = int(total)
            startTime = time.time()
            for data in response.iter_content(chunk_size=max(int(total/1000), 1024*1024)):
                downloaded += len(data)
                f.write(data)
                done = int(50*downloaded/total)
                percentage = (downloaded / total) * 100
                elapsedTime = time.time() - startTime
                avgKBPerSecond = (downloaded / 1024) / elapsedTime
                avgSpeedString = '{:.2f} KB/s'.format(avgKBPerSecond)
                if (avgKBPerSecond > 1024):
                    avgMBPerSecond = avgKBPerSecond / 1024
                    avgSpeedString = '{:.2f} MB/s'.format(avgMBPerSecond)
                sys.stdout.write('\r[{}{}] {:.2f}% ({})     '.format('█' * done, '.' * (50-done), percentage, avgSpeedString))
                sys.stdout.flush()
    sys.stdout.write('\n')

def YesOrNo():
    while True:
        reply = str(input('[Y/N]: ')).lower().strip()
        if reply[:1] == 'y':
            return True
        if reply[:1] == 'n':
            return False

def IsValidIDE(ide):
    if (ide == 'vs2019'):
        return True
    if (ide == 'vs2022'):
        return True
    else:
        return False

def ChooseIDE():
    while True:
        reply = str(input('[vs2019/vs2022]: ')).lower().strip()
        if (len(reply) > 0):
            if (not IsValidIDE(reply)):
                print(f"Unsupported IDE argumet {reply}! Supported IDEs: [vs2019, vs2022]")
            else:
                return reply