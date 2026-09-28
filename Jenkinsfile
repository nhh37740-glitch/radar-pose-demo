pipeline {
    agent any
    options {
        timestamps()
        disableConcurrentBuilds()
    }
    stages {
        stage('Prepare complete recorded data') {
            steps {
                sh '''
                    set -eu
                    if [ ! -f runtime-data/manifest.json ]; then
                        test -n "${RADAR_SOURCE_DIRECTORY:-}" || {
                            echo 'Complete runtime-data pack missing; set RADAR_SOURCE_DIRECTORY to the server-side original data directory'
                            exit 1
                        }
                        python3 tools/prepare_full_data.py --source "$RADAR_SOURCE_DIRECTORY"
                    fi
                '''
            }
        }
        stage('Validate and package module') {
            steps {
                sh 'python3 tools/package_release.py'
                archiveArtifacts artifacts: 'dist/*.zip,dist/*.zip.sha256,dist/*.manifest.json', fingerprint: true
            }
        }
        stage('Build isolated static container') {
            steps {
                sh 'sudo docker compose --project-name radar-pose-demo build'
            }
        }
        stage('Deploy and verify module') {
            steps {
                sh '''
                    set -eu
                    sudo docker compose --project-name radar-pose-demo up -d --no-build
                    cid="$(sudo docker compose --project-name radar-pose-demo ps -q radar)"
                    test -n "$cid"
                    for attempt in $(seq 1 24); do
                        status="$(sudo docker inspect --format '{{.State.Health.Status}}' "$cid")"
                        if [ "$status" = healthy ]; then break; fi
                        if [ "$status" = unhealthy ]; then
                            sudo docker compose --project-name radar-pose-demo logs --tail=100 radar
                            exit 1
                        fi
                        sleep 5
                    done
                    if [ "$status" != healthy ]; then
                        sudo docker compose --project-name radar-pose-demo logs --tail=100 radar
                        exit 1
                    fi
                    base="http://127.0.0.1:18104"
                    curl -fsS "$base/index.html" | grep -q '不在浏览器中运行模型推理'
                    curl -fsS "$base/data-license.html" | grep -q 'CC BY-NC-SA 4.0'
                    curl -fsS "$base/full/manifest.json" | grep -q '"sampleCount":7203'
                    curl -fsS "$base/full/chunks/page-00030.json" | grep -q '"startFrame":7200'
                    curl -fsS "$base/full/radar/1547559404102392.jpg" -o /dev/null
                    curl -fsS "$base/full/stereo/1547559404126575.jpg" -o /dev/null
                '''
            }
        }
    }
}
