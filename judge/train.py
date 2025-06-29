import os
import pandas as pd
import numpy as np
import random
from collections import deque
import torch
import torch.nn as nn
import torch.optim as optim
from sklearn.model_selection import train_test_split

# 数据集路径（全局变量，修改这里以更改数据集位置）
DATASET_BENIGN_DIR = r'dataset/benign/benign_vec'  # 修改为你的良性数据集路径
DATASET_RANSOMWARE_DIR = r'dataset/ransomware/ransomware_vec'  # 修改为你的恶意软件数据集路径
MODEL_PATH = r'model.pth'  # 保存模型的路径
WEIGHTS_PATH = r'model_weights.bin'  # 保存模型权重的二进制文件路径

# 数据加载函数
def load_data(directory, label):
    data = []
    for filename in os.listdir(directory):
        if filename.endswith('.csv'):
            filepath = os.path.join(directory, filename)
            df = pd.read_csv(filepath)
            # 每次取10行数据（10*4=40维）
            for start in range(0, len(df), 10):
                end = start + 10
                if end <= len(df):  # 确保不超过数据长度
                    chunk = df.iloc[start:end].values.flatten()
                    if len(chunk) == 40:  # 确保维度为40
                        data.append(chunk)
    labels = [label] * len(data)
    return data, labels

# 强化学习环境
class MalwareDetectionEnv:
    def __init__(self, benign_data, ransomware_data):
        self.data = benign_data + ransomware_data
        self.labels = [0] * len(benign_data) + [1] * len(ransomware_data)
        self.num_samples = len(self.data)
        self.current_idx = 0

    def reset(self):
        self.current_idx = random.randint(0, self.num_samples - 1)
        return self.data[self.current_idx]

    def step(self, action):
        true_label = self.labels[self.current_idx]
        reward = 1 if action == true_label else -1
        done = True
        return None, reward, done

# DQN网络
class DQN(nn.Module):
    def __init__(self, input_dim, output_dim):
        super(DQN, self).__init__()
        self.fc1 = nn.Linear(input_dim, 128)
        self.fc2 = nn.Linear(128, 64)
        self.fc3 = nn.Linear(64, output_dim)
    
    def forward(self, x):
        x = torch.relu(self.fc1(x))
        x = torch.relu(self.fc2(x))
        return self.fc3(x)

# 经验回放
class ReplayMemory:
    def __init__(self, capacity):
        self.memory = deque(maxlen=capacity)

    def push(self, state, action, reward, next_state):
        self.memory.append((state, action, reward, next_state))

    def sample(self, batch_size):
        return random.sample(self.memory, batch_size)

    def __len__(self):
        return len(self.memory)

# 智能体类
class MalwareDetectionAgent:
    def __init__(self, input_dim, output_dim, device):
        self.device = device
        self.policy_net = DQN(input_dim, output_dim).to(device)
        self.target_net = DQN(input_dim, output_dim).to(device)
        self.target_net.load_state_dict(self.policy_net.state_dict())
        self.target_net.eval()
        self.optimizer = optim.Adam(self.policy_net.parameters())
        self.memory = ReplayMemory(10000)
        self.steps_done = 0
        self.eps_start = 0.9
        self.eps_end = 0.05
        self.eps_decay = 200
        self.gamma = 0.99

    def select_action(self, state):
        eps_threshold = self.eps_end + (self.eps_start - self.eps_end) * \
            np.exp(-1. * self.steps_done / self.eps_decay)
        self.steps_done += 1
        if random.random() > eps_threshold:
            with torch.no_grad():
                return self.policy_net(state).argmax().item()
        else:
            return random.randrange(2)

    def optimize_model(self, batch_size):
        if len(self.memory) < batch_size:
            return
        transitions = self.memory.sample(batch_size)
        batch_state, batch_action, batch_reward, batch_next_state = zip(*transitions)

        batch_state = torch.stack([torch.FloatTensor(s) for s in batch_state]).to(self.device)
        batch_action = torch.LongTensor(batch_action).to(self.device)
        batch_reward = torch.FloatTensor(batch_reward).to(self.device)

        q_values = self.policy_net(batch_state).gather(1, batch_action.unsqueeze(1))
        target_q_values = batch_reward  # 单步任务

        loss = nn.MSELoss()(q_values, target_q_values.unsqueeze(1))
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

    def update_target_net(self):
        self.target_net.load_state_dict(self.policy_net.state_dict())

# 保存模型权重为二进制格式
def save_weights_to_binary(model, filepath):
    # 按顺序收集权重和偏置
    weights = []
    for name, param in model.named_parameters():
        weights.append(param.cpu().detach().numpy().flatten())
    weights = np.concatenate(weights)
    weights.tofile(filepath)
    print(f"模型权重已保存至: {filepath}")

# 训练函数
def train_agent(benign_train, ransomware_train):
    input_dim = 40  # 修改为10*4=40
    output_dim = 2
    batch_size = 32
    num_episodes = 8000
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    env = MalwareDetectionEnv(benign_train, ransomware_train)
    agent = MalwareDetectionAgent(input_dim, output_dim, device)

    for episode in range(num_episodes):
        state = env.reset()
        state = torch.FloatTensor(state).to(device)
        total_reward = 0

        action = agent.select_action(state)
        next_state, reward, done = env.step(action)
        total_reward += reward

        agent.memory.push(state, action, reward, next_state)
        agent.optimize_model(batch_size)

        if episode % 10 == 0:
            agent.update_target_net()

        if episode % 100 == 0:  # 每100个回合打印一次
            print(f"Episode {episode}, Total Reward: {total_reward}")

    # 保存训练好的模型（原始.pth格式）
    torch.save(agent.policy_net.state_dict(), MODEL_PATH)
    print(f"模型已保存至: {MODEL_PATH}")

    # 保存模型权重为二进制格式（供C语言推理使用）
    save_weights_to_binary(agent.policy_net, WEIGHTS_PATH)

    return agent

# 测试函数
def test_agent(agent, benign_test, ransomware_test):
    env = MalwareDetectionEnv(benign_test, ransomware_test)
    correct = 0
    total = min(1000, len(benign_test) + len(ransomware_test))  # 确保测试次数不超过测试集大小
    device = agent.device

    for _ in range(total):
        state = env.reset()
        state = torch.FloatTensor(state).to(device)
        with torch.no_grad():
            action = agent.policy_net(state).argmax().item()
        _, reward, _ = env.step(action)
        if reward == 1:
            correct += 1

    accuracy = correct / total
    print(f"测试准确率: {accuracy}")

# 主程序
if __name__ == "__main__":
    # 加载完整数据集
    benign_data, _ = load_data(DATASET_BENIGN_DIR, 0)
    ransomware_data, _ = load_data(DATASET_RANSOMWARE_DIR, 1)

    # 分割数据集（80% 训练，20% 测试）
    benign_train, benign_test = train_test_split(benign_data, test_size=0.2, random_state=42)
    ransomware_train, ransomware_test = train_test_split(ransomware_data, test_size=0.2, random_state=42)

    # 训练
    agent = train_agent(benign_train, ransomware_train)
    # 测试
    test_agent(agent, benign_test, ransomware_test)