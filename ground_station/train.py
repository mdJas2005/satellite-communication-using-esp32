"""Train an image classifier on dataset/<class_name>/*.jpg  (transfer learning, MobileNetV2).
Usage:  python train.py --epochs 10
"""
import argparse
import json

import tensorflow as tf
import config

p = argparse.ArgumentParser()
p.add_argument("--epochs", type=int, default=10)
p.add_argument("--data", default=str(config.DATASET_DIR))
args = p.parse_args()

SIZE = (224, 224)
common = dict(image_size=SIZE, batch_size=32, seed=42, validation_split=0.2, label_mode="int")
train = tf.keras.utils.image_dataset_from_directory(args.data, subset="training", **common)
val = tf.keras.utils.image_dataset_from_directory(args.data, subset="validation", **common)
classes = train.class_names
print("Classes:", classes)
AUTOTUNE = tf.data.AUTOTUNE
train, val = train.prefetch(AUTOTUNE), val.prefetch(AUTOTUNE)

base = tf.keras.applications.MobileNetV2(include_top=False, weights="imagenet", input_shape=SIZE + (3,))
base.trainable = False

inputs = tf.keras.Input(SIZE + (3,))
x = tf.keras.layers.RandomFlip("horizontal")(inputs)
x = tf.keras.layers.RandomRotation(0.1)(x)
x = tf.keras.layers.Rescaling(1 / 127.5, offset=-1)(x)       # MobileNetV2 expects [-1, 1]
x = base(x, training=False)
x = tf.keras.layers.GlobalAveragePooling2D()(x)
x = tf.keras.layers.Dropout(0.2)(x)
outputs = tf.keras.layers.Dense(len(classes), activation="softmax")(x)
model = tf.keras.Model(inputs, outputs)

model.compile(optimizer="adam", loss="sparse_categorical_crossentropy", metrics=["accuracy"])
model.fit(train, validation_data=val, epochs=args.epochs)
model.save(config.MODEL_PATH)
config.LABELS_PATH.write_text(json.dumps(classes))
print(f"Saved {config.MODEL_PATH} and {config.LABELS_PATH}")
