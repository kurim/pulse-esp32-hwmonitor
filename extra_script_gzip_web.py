Import("env")
import gzip
import os

# board_build.embed_files bettet die Datei so ein, wie sie auf der Platte
# liegt - die Kompression muss also vor dem SCons-Build passieren, nicht
# durch den Server zur Laufzeit. web/dashboard.html bleibt die editierbare
# Quelle, web/dashboard.html.gz ist das generierte, tatsaechlich eingebettete
# Artefakt (wird bei jedem Build neu erzeugt, nicht eingecheckt).
def gzip_file(src, dst):
    with open(src, "rb") as f_in:
        data = f_in.read()
    with open(dst, "wb") as raw_out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw_out, mtime=0) as f_out:
            f_out.write(data)

web_dir = os.path.join(env["PROJECT_DIR"], "web")
gzip_file(os.path.join(web_dir, "dashboard.html"), os.path.join(web_dir, "dashboard.html.gz"))
gzip_file(os.path.join(web_dir, "favicon.svg"), os.path.join(web_dir, "favicon.svg.gz"))
