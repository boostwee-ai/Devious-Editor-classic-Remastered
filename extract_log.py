import subprocess

print("Started log extraction...")
def main():
    try:
        log = subprocess.check_output(r'"C:\Program Files\GitHub CLI\gh.exe" run view 24126882359 --log-failed', shell=True)
        with open(r'job_log.txt', 'wb') as f:
            f.write(log)
        lines = log.decode('utf-8', errors='ignore').split('\n')
        print('\n'.join(lines[-400:]))
    except Exception as e:
        print("Error:", e)

if __name__ == '__main__':
    main()
