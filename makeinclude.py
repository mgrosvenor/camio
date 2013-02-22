import os

os.system("mkdir -p include")

for l in open(".tmp.dirs"):
    cmd = "mkdir -p include/%s" %( l[2:-1] )
    print cmd
    os.system(cmd)

for l in open(".tmp.headers"):
    cmd = "cp %s include/%s" %( l[2:-1], l[2:-1] )
    print cmd
    os.system(cmd)

    
    
