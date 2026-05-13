FROM node:22-bookworm

# Install system dependencies
RUN apt-get update && apt-get install -y \
  git \
  curl \
  jq \
  build-essential \
  cmake \
  ninja-build \
  clang \
  && rm -rf /var/lib/apt/lists/*

# Install GitHub CLI
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
  | dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg \
  && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
  | tee /etc/apt/sources.list.d/github-cli.list > /dev/null \
  && apt-get update && apt-get install -y gh \
  && rm -rf /var/lib/apt/lists/*

# Build-args for UID/GID alignment: sandcastle docker build-image
# defaults these to the host user's UID/GID so image-built files
# and bind-mounted files share an owner without runtime chown.
#
# macOS workaround: on macOS the host primary GID is 20 (staff), which
# already exists in the Debian base as "dialout" and so collides with
# `groupmod -g 20 node`. Sandcastle's CLI unconditionally injects host
# UID/GID via --build-arg, so we accept AGENT_GID but ignore it, and use
# TARGET_GID for the actual in-container GID. Side effect: files written
# through bind-mounts will be owned by your host user (UID match) but
# group 1000 (no group-name match with host "staff"), so group-write
# workflows on shared dirs won't work — owner reads/writes are fine.
ARG AGENT_UID=1000
ARG AGENT_GID=1000
ARG TARGET_GID=1000

# Rename the base image's "node" user to "agent" and align UID/GID.
RUN groupmod -g $TARGET_GID node \
 && usermod -u $AGENT_UID -g $TARGET_GID -d /home/agent -m -l agent node
USER ${AGENT_UID}:${TARGET_GID}

# Install Claude Code CLI
RUN curl -fsSL https://claude.ai/install.sh | bash

# Add Claude to PATH
ENV PATH="/home/agent/.local/bin:$PATH"

WORKDIR /home/agent

# In worktree sandbox mode, Sandcastle bind-mounts the git worktree at /home/agent/workspace
# and overrides the working directory to /home/agent/workspace at container start.
# Structure your Dockerfile so that /home/agent/workspace can serve as the project root.
ENTRYPOINT ["sleep", "infinity"]
