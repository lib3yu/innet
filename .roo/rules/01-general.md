

## 编译innet库
```bash
make clean
make 
```

## 运行demo

```bash
make clean
make demo
# demo_industry
LD_LIBRARY_PATH=lib/ ./bin/demo_industry
# demo_pubsub
LD_LIBRARY_PATH=lib/ ./bin/demo_pubsub
```
