#!/usr/bin/env sh

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
log_dir="$project_dir/logs"
log_file="$log_dir/serial.log"
pid_file="$log_dir/serial_logger.pid"
python_bin=${PYTHON_BIN:-python3}

select_python() {
    if "$python_bin" -c 'import serial' >/dev/null 2>&1; then
        return
    fi
    idf_python="$HOME/.espressif/python_env/idf5.5_py3.12_env/bin/python"
    if [ -x "$idf_python" ] && "$idf_python" -c 'import serial' >/dev/null 2>&1; then
        python_bin="$idf_python"
        return
    fi
    echo "未找到带 pyserial 的 Python；请设置 PYTHON_BIN。" >&2
    exit 1
}

is_running() {
    [ -f "$pid_file" ] && kill -0 "$(cat "$pid_file")" 2>/dev/null
}

case "${1:-status}" in
    start)
        mkdir -p "$log_dir"
        if is_running; then
            echo "串口日志已运行，PID $(cat "$pid_file")"
            exit 0
        fi
        rm -f "$pid_file"
        select_python
        port=${SERIAL_PORT:-/dev/ttyACM0}
        baud=${SERIAL_BAUD:-115200}
        setsid "$python_bin" "$project_dir/tools/serial_logger.py" \
            --port "$port" --baud "$baud" --log "$log_file" >>"$log_file" 2>&1 &
        echo $! >"$pid_file"
        echo "串口日志已启动：$log_file（PID $(cat "$pid_file")）"
        ;;
    stop)
        if is_running; then
            kill "$(cat "$pid_file")"
            echo "串口日志已停止"
        else
            echo "串口日志未运行"
        fi
        rm -f "$pid_file"
        ;;
    status)
        if is_running; then
            echo "串口日志运行中，PID $(cat "$pid_file")，文件：$log_file"
        else
            echo "串口日志未运行，文件：$log_file"
            exit 1
        fi
        ;;
    tail)
        mkdir -p "$log_dir"
        touch "$log_file"
        tail -n 100 -f "$log_file"
        ;;
    *)
        echo "用法：$0 {start|stop|status|tail}" >&2
        exit 2
        ;;
esac
