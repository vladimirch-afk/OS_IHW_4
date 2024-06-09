if [[ $# -ne 4 ]] ; 
then
    echo "You should pass 4 args: ip, port, work_time_1, work_time_2"
    exit 1
fi

./first $1 $2 $3 &
./second $1 $2 $4 &
