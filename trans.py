#! /usr/bin/python
# coding:utf-8
import codecs
import sys
import os

def trans(filepath):
    print "dealwith"+filepath
    data = codecs.EncodedFile(file(filepath,'r'),'utf-8','gbk').read()
    file(filepath, 'w').write(data)
    print filepath+" gbk ==> utf-8 "

def main():
    for root, dirs, files in os.walk("."):
        for name in files:
            filepath = os.path.join(root, name)
            try:
                f = codecs.open(filepath, 'r', encoding = "utf-8")
                f.readline()
                f.seek(0)
                f.close()
            except UnicodeDecodeError:
                trans(filepath)

if __name__ == '__main__':
    main()
