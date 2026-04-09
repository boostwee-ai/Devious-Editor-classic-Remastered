import subprocess
def main():
    try:
        log = subprocess.check_output(r'"C:\Program Files\GitHub CLI\gh.exe" api repos/geode-sdk/example-mod/contents/CMakeLists.txt -H "Accept: application/vnd.github.v3.raw"', shell=True)
        print(log.decode('utf-8'))
    except Exception as e:
        print("Error:", e)

if __name__ == '__main__':
    main()
