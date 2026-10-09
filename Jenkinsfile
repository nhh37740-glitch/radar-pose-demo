pipeline {
    agent any
    options {
        timestamps()
        disableConcurrentBuilds()
    }
    parameters {
        string(name: 'RADAR_SOURCE_DIRECTORY', defaultValue: '/home/ubuntu/radar-full-source', description: 'Private server directory with the original radar pose JS and paired JPEGs')
    }
    stages {
        stage('Prepare complete recorded data') {
            steps {
                sh '''
                    set -eu
                    if [ ! -f runtime-data/manifest.json ]; then
                        if [ -e runtime-data ]; then
                            echo 'Incomplete runtime-data directory exists; refusing to mutate a possible live mount'
                            exit 1
                        fi
                        source_directory="${RADAR_SOURCE_DIRECTORY:-/home/ubuntu/radar-full-source}"
                        if [ ! -f "$source_directory/data/method_comparison_jan15_cfear_lite_pose_data.js" ] || [ ! -d "$source_directory/assets/radar" ] || [ ! -d "$source_directory/assets/stereo" ]; then
                            echo 'Full Radar source is missing its pose JS or paired image directories'
                            exit 1
                        fi
                        python3 tools/prepare_full_data.py --source "$source_directory"
                    fi
                '''
            }
        }
        stage('Validate and package module') {
            steps {
                sh 'node tools/verify_playback.cjs'
                sh 'python3 tools/package_release.py'
                archiveArtifacts artifacts: 'dist/*.zip,dist/*.zip.sha256,dist/*.manifest.json', fingerprint: true
            }
        }
        stage('Build isolated static container') {
            steps {
                sh '''
                    set -eu
                    old_cid="$(sudo docker compose --project-name radar-pose-demo ps -q radar 2>/dev/null || true)"
                    if [ -n "$old_cid" ]; then
                        old_image="$(sudo docker inspect --format '{{.Image}}' "$old_cid")"
                        sudo docker image tag "$old_image" radar-pose-demo:rollback
                    fi
                    sudo docker compose --project-name radar-pose-demo build
                '''
            }
        }
        stage('Verify candidate without switching production') {
            steps {
                sh '''
                    set -eu
                    candidate=radar-pose-demo-candidate
                    sudo docker rm -f "$candidate" >/dev/null 2>&1 || true
                    trap 'sudo docker rm -f radar-pose-demo-candidate >/dev/null 2>&1 || true' EXIT
                    sudo docker run -d --name "$candidate" --mount "type=bind,source=$PWD/runtime-data,target=/usr/share/nginx/html/full,readonly" -p 127.0.0.1::80 radar-pose-demo:local
                    for attempt in $(seq 1 24); do
                        status="$(sudo docker inspect --format '{{.State.Health.Status}}' "$candidate")"
                        if [ "$status" = healthy ]; then break; fi
                        if [ "$status" = unhealthy ]; then
                            sudo docker logs --tail=100 "$candidate"
                            exit 1
                        fi
                        sleep 5
                    done
                    if [ "$status" != healthy ]; then
                        sudo docker logs --tail=100 "$candidate"
                        exit 1
                    fi
                    candidate_port="$(sudo docker port "$candidate" 80/tcp | sed -n 's/.*://p')"
                    test -n "$candidate_port"
                    base="http://127.0.0.1:$candidate_port"
                    curl -fsS "$base/index.html" | grep -q '不在浏览器中运行模型推理'
                    curl -fsS "$base/data-license.html" | grep -q 'CC BY-NC-SA 4.0'
                    curl -fsS "$base/full/manifest.json" | grep -q '"sampleCount":7203'
                    curl -fsS "$base/full/chunks/page-00030.json" | grep -Fq '[7202,1547559404102392'
                    curl -fsSI "$base/full/radar/1547559404102392.jpg" -o /dev/null
                    curl -fsSI "$base/full/stereo/1547559404126575.jpg" -o /dev/null
                '''
            }
        }
        stage('Switch production and verify') {
            steps {
                sh '''
                    set -eu
                    switched=0
                    rollback_on_failure() {
                        result=$?
                        if [ "$result" -ne 0 ] && [ "$switched" -eq 1 ] && sudo docker image inspect radar-pose-demo:rollback >/dev/null 2>&1; then
                            sudo docker image tag radar-pose-demo:rollback radar-pose-demo:local
                            sudo docker compose --project-name radar-pose-demo up -d --force-recreate --no-build || true
                        fi
                    }
                    trap rollback_on_failure EXIT
                    switched=1
                    sudo docker compose --project-name radar-pose-demo up -d --no-build
                    cid="$(sudo docker compose --project-name radar-pose-demo ps -q radar)"
                    test -n "$cid"
                    for attempt in $(seq 1 24); do
                        status="$(sudo docker inspect --format '{{.State.Health.Status}}' "$cid")"
                        if [ "$status" = healthy ]; then break; fi
                        if [ "$status" = unhealthy ]; then exit 1; fi
                        sleep 5
                    done
                    test "$status" = healthy
                    base="http://127.0.0.1:18104"
                    curl -fsS "$base/full/manifest.json" | grep -q '"sampleCount":7203'
                    curl -fsS "$base/full/chunks/page-00030.json" | grep -Fq '[7202,1547559404102392'
                    curl -fsSI "$base/full/radar/1547559404102392.jpg" -o /dev/null
                    curl -fsSI "$base/full/stereo/1547559404126575.jpg" -o /dev/null
                '''
            }
        }
    }
}
