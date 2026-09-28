pipeline {
    agent any
    options {
        timestamps()
        disableConcurrentBuilds()
    }
    stages {
        stage('Validate and package module') {
            steps {
                sh 'python3 tools/package_release.py'
                archiveArtifacts artifacts: 'dist/*.zip,dist/*.zip.sha256,dist/*.manifest.json', fingerprint: true
            }
        }
        stage('Build isolated static container') {
            steps {
                sh 'docker compose --project-name radar-pose-demo build'
            }
        }
        stage('Deploy and verify module') {
            steps {
                sh '''
                    set -eu
                    docker compose --project-name radar-pose-demo up -d --no-build
                    cid="$(docker compose --project-name radar-pose-demo ps -q radar)"
                    test -n "$cid"
                    for attempt in $(seq 1 24); do
                        status="$(docker inspect --format '{{.State.Health.Status}}' "$cid")"
                        if [ "$status" = healthy ]; then break; fi
                        if [ "$status" = unhealthy ]; then
                            docker compose --project-name radar-pose-demo logs --tail=100 radar
                            exit 1
                        fi
                        sleep 5
                    done
                    if [ "$status" != healthy ]; then
                        docker compose --project-name radar-pose-demo logs --tail=100 radar
                        exit 1
                    fi
                    base="http://127.0.0.1:18104"
                    curl -fsS "$base/index.html" | grep -q '不在浏览器中运行模型推理'
                    curl -fsS "$base/data-license.html" | grep -q 'CC BY-NC-SA 4.0'
                    curl -fsS "$base/data/pose-excerpt.js" | grep -q '"excerptStartFrame":0'
                    curl -fsS "$base/assets/radar/1547557604078984.jpg" -o /dev/null
                    curl -fsS "$base/assets/stereo/1547557604081434.jpg" -o /dev/null
                '''
            }
        }
    }
}
