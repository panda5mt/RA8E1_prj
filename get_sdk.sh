#!/bin/sh

echo -e '#!/bin/bash\nNOW=`date "+%Y%m%d_%H%M%S"`\ngit add .\n# git commit -m "automatically uploaded at "$NOW\ngit commit -m "Automatically uploaded"\ngit push origin HEAD' > add_git.sh
chmod +x add_git.sh